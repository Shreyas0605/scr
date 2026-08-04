#include "VideoBackground.h"
#include <mfapi.h>
#include <mferror.h>
#include <propvarutil.h>
#include <cstring>
#include <chrono>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")

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
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) && buffer) {
            BYTE* data = nullptr;
            DWORD maxLen = 0, curLen = 0;
            if (SUCCEEDED(buffer->Lock(&data, &maxLen, &curLen))) {
                if (m_width > 0 && m_height > 0) {
                    const UINT32 stride = m_width * 4;
                    const size_t byteCount = static_cast<size_t>(stride) * m_height;

                    // Guard against a short buffer (shouldn't happen for
                    // RGB32 at this resolution, but a malformed/truncated
                    // source could produce one) rather than reading past
                    // the mapped region.
                    if (curLen >= byteCount) {
                        std::lock_guard<std::mutex> lock(m_frameMutex);
                        m_pendingPixels.resize(byteCount);
                        memcpy(m_pendingPixels.data(), data, byteCount);
                        m_pendingWidth = m_width;
                        m_pendingHeight = m_height;
                        m_pendingStride = stride;
                        m_hasPendingFrame = true;
                    }
                }
                buffer->Unlock();
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
