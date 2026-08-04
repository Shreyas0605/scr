#include "VideoBackground.h"
#include <mfapi.h>
#include <mferror.h>
#include <mfobjects.h>
#include <propvarutil.h>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <timeapi.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "winmm.lib")

namespace fcs::background {

bool VideoBackground::Open(ID2D1DeviceContext* ctx, const std::wstring& path, bool loop, bool muted) {
    Shutdown();
    m_ctx = ctx;
    m_loop = loop;
    m_muted = muted;

    ComPtr<IMFAttributes> attrs;
    HRESULT hr = MFCreateAttributes(&attrs, 2);
    if (FAILED(hr)) return false;
    // Disable audio processing entirely when muted (saves CPU and avoids
    // needing an audio session for a background loop). Video-only stream
    // selection is applied after opening via SetStreamSelection.
    attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
    // Required for the RGB32 SetCurrentMediaType request below to succeed
    // on real-world video: most decoders (H.264 in particular) natively
    // output NV12, and without this flag the Source Reader refuses to
    // color-convert, so SetCurrentMediaType fails and Open() never
    // succeeds - the video silently never starts (background stays black)
    // rather than throwing an obvious error.
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    hr = MFCreateSourceReaderFromURL(path.c_str(), attrs.Get(), &m_reader);
    if (FAILED(hr) || !m_reader) return false;

    // Select only the first video stream; deselect audio if muted.
    m_reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    m_reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
    if (!m_muted) {
        m_reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);
    }

    ComPtr<IMFMediaType> outputType;
    hr = MFCreateMediaType(&outputType);
    if (FAILED(hr)) return false;
    outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = m_reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                        nullptr, outputType.Get());
    if (FAILED(hr)) {
        m_reader.Reset();
        return false; // no compatible decoder for this codec/container at all
    }

    ComPtr<IMFMediaType> actualType;
    m_reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &actualType);
    if (actualType) {
        MFGetAttributeSize(actualType.Get(), MF_MT_FRAME_SIZE, &m_width, &m_height);
        UINT32 num = 30, den = 1;
        if (SUCCEEDED(MFGetAttributeRatio(actualType.Get(), MF_MT_FRAME_RATE, &num, &den)) && num > 0) {
            m_frameDuration100ns = static_cast<LONGLONG>(10'000'000.0 * den / num);
        }
    }

    m_stopRequested = false;
    m_thread = CreateThread(nullptr, 0, &VideoBackground::DecodeThreadProc, this, 0, nullptr);
    if (m_thread) {
        // Video decode is latency-sensitive: if this thread gets starved by
        // normal scheduling under system load, decoded frames arrive late
        // and playback visibly falls behind/stutters. ABOVE_NORMAL is a
        // standard, conservative bump for real-time media threads - high
        // enough to avoid starvation, not so high it fights the render
        // thread or the rest of the system.
        SetThreadPriority(m_thread, THREAD_PRIORITY_ABOVE_NORMAL);
    }
    return m_thread != nullptr;
}

DWORD WINAPI VideoBackground::DecodeThreadProc(LPVOID param) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    static_cast<VideoBackground*>(param)->DecodeLoop();
    CoUninitialize();
    return 0;
}

void VideoBackground::DecodeLoop() {
    using Clock = std::chrono::steady_clock;

    // Windows' default timer resolution (~15.6ms) makes Sleep() far less
    // precise than the frame intervals we're trying to pace to (e.g.
    // ~33ms for 30fps) - raising it to 1ms here is the standard technique
    // used by games/media players for exactly this reason. Restored to
    // whatever it was before on every exit path (all breaks below fall
    // through to just after the loop).
    timeBeginPeriod(1);

    while (!m_stopRequested) {
        const auto iterationStart = Clock::now();

        DWORD streamIndex = 0, flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;

        HRESULT hr = m_reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0,
                                           &streamIndex, &flags, &timestamp, &sample);
        if (FAILED(hr)) break;

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (!m_loop) break;
            PROPVARIANT var;
            PropVariantInit(&var);
            var.vt = VT_I8;
            var.hVal.QuadPart = 0;
            m_reader->SetCurrentPosition(GUID_NULL, var);
            PropVariantClear(&var);
            continue;
        }

        if (!sample) {
            Sleep(1);
            continue;
        }

        ComPtr<IMFMediaBuffer> buffer;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) && buffer && m_width > 0 &&
            m_height > 0) {
            const UINT32 stride = m_width * 4;
            const size_t byteCount = static_cast<size_t>(stride) * m_height;
            std::vector<BYTE> localFrame(byteCount);
            bool copied = false;

            // Preferred path: IMF2DBuffer2::Copy2DTo copies into our
            // tightly-packed destination stride, correcting for whatever
            // the source buffer's actual row pitch is (including padded/
            // aligned rows and bottom-up/negative-pitch layouts). A plain
            // 1-D Lock() + memcpy, used previously, silently assumed the
            // buffer had zero row padding - true only for some
            // encoders/resolutions, and wrong for others, which is exactly
            // what produced sheared "moving lines" on one video and a
            // diagonally-duplicated frame on another: both are the same
            // wrong-stride bug, just manifesting differently per source.
            ComPtr<IMF2DBuffer2> buffer2D2;
            if (SUCCEEDED(buffer.As(&buffer2D2)) && buffer2D2) {
                if (SUCCEEDED(buffer2D2->Copy2DTo(localFrame.data(), static_cast<LONG>(stride),
                                                   static_cast<DWORD>(byteCount)))) {
                    copied = true;
                }
            }

            // Fallback: manual row-by-row copy via the older IMF2DBuffer
            // interface, honoring whatever pitch it reports (still correct
            // for padded/bottom-up buffers, just without Copy2DTo's
            // convenience).
            if (!copied) {
                ComPtr<IMF2DBuffer> buffer2D;
                if (SUCCEEDED(buffer.As(&buffer2D)) && buffer2D) {
                    BYTE* scanline0 = nullptr;
                    LONG pitch = 0;
                    if (SUCCEEDED(buffer2D->Lock2D(&scanline0, &pitch))) {
                        const LONG absPitch = pitch < 0 ? -pitch : pitch;
                        const size_t rowBytes = std::min<size_t>(stride, static_cast<size_t>(absPitch));
                        for (UINT32 y = 0; y < m_height; ++y) {
                            // Negative pitch means the data is stored
                            // bottom-up; walk source rows in reverse while
                            // still writing the destination top-down.
                            const BYTE* srcRow =
                                (pitch < 0)
                                    ? scanline0 + static_cast<ptrdiff_t>(pitch) *
                                                      static_cast<ptrdiff_t>(m_height - 1 - y)
                                    : scanline0 + static_cast<ptrdiff_t>(pitch) * static_cast<ptrdiff_t>(y);
                            memcpy(localFrame.data() + static_cast<size_t>(y) * stride, srcRow, rowBytes);
                        }
                        buffer2D->Unlock2D();
                        copied = true;
                    }
                }
            }

            // Last-resort fallback for a buffer type that supports
            // neither 2-D interface: assumes zero row padding, matching
            // the original (buggy-on-some-sources) behavior. Kept only so
            // playback doesn't stop outright on an exotic buffer type;
            // the two paths above should handle virtually everything in
            // practice.
            if (!copied) {
                BYTE* data = nullptr;
                DWORD maxLen = 0, curLen = 0;
                if (SUCCEEDED(buffer->Lock(&data, &maxLen, &curLen))) {
                    if (curLen >= byteCount) {
                        memcpy(localFrame.data(), data, byteCount);
                        copied = true;
                    }
                    buffer->Unlock();
                }
            }

            if (copied) {
                // Swapping the already-populated local buffer into
                // m_pendingPixels is O(1), so the render thread's
                // SyncFrame() is only ever blocked for a pointer swap, not
                // a multi-megabyte copy.
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_pendingPixels.swap(localFrame);
                m_pendingWidth = m_width;
                m_pendingHeight = m_height;
                m_pendingStride = stride;
                m_hasPendingFrame = true;
            }
        }

        // Pace decoding to the source frame rate - but account for how
        // long decoding this frame actually took, rather than always
        // sleeping the full frame interval on top of it. On a slow
        // decode (large/high-bitrate video), always adding the full
        // interval compounds delay frame after frame, which is exactly
        // what makes playback feel like it's falling further and further
        // behind ("laggy") rather than just running at a steady, if
        // reduced, rate.
        const auto targetDuration = std::chrono::microseconds(m_frameDuration100ns / 10);
        const auto elapsed = Clock::now() - iterationStart;
        if (elapsed < targetDuration) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                targetDuration - elapsed);
            if (remaining.count() > 0) Sleep(static_cast<DWORD>(remaining.count()));
        }
    }

    timeEndPeriod(1);
}

ComPtr<ID2D1Bitmap1> VideoBackground::CurrentFrame() {
    return m_currentBitmap;
}

void VideoBackground::SyncFrame(ID2D1DeviceContext* ctx) {
    if (!ctx) return;

    std::vector<BYTE> pixels;
    UINT32 w = 0, h = 0, stride = 0;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (!m_hasPendingFrame) return;
        pixels.swap(m_pendingPixels);
        w = m_pendingWidth;
        h = m_pendingHeight;
        stride = m_pendingStride;
        m_hasPendingFrame = false;
    }
    if (pixels.empty() || w == 0 || h == 0) return;

    // Reuse the existing bitmap (just refresh its pixels) whenever the
    // frame size hasn't changed, rather than allocating a new ID2D1Bitmap1
    // every single frame - CopyFromMemory is far cheaper than CreateBitmap.
    if (m_currentBitmap) {
        D2D1_SIZE_U existing = m_currentBitmap->GetPixelSize();
        if (existing.width == w && existing.height == h) {
            const D2D1_RECT_U dest = D2D1::RectU(0, 0, w, h);
            if (SUCCEEDED(m_currentBitmap->CopyFromMemory(&dest, pixels.data(), stride))) return;
        }
    }

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    ComPtr<ID2D1Bitmap1> newBitmap;
    if (SUCCEEDED(ctx->CreateBitmap(D2D1::SizeU(w, h), pixels.data(), stride, props, &newBitmap))) {
        m_currentBitmap = newBitmap;
    }
}

void VideoBackground::Shutdown() {
    m_stopRequested = true;
    if (m_thread) {
        WaitForSingleObject(m_thread, 2000);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }
    m_reader.Reset();
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_pendingPixels.clear();
        m_hasPendingFrame = false;
    }
    m_currentBitmap.Reset(); // only ever touched on the main thread, safe without the lock
}

} // namespace fcs::background
