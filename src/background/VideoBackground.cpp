#include "VideoBackground.h"
#include <mfapi.h>
#include <mferror.h>
#include <propvarutil.h>

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
    HRESULT hr = MFCreateAttributes(&attrs, 1);
    if (FAILED(hr)) return false;
    // Disable audio processing entirely when muted (saves CPU and avoids
    // needing an audio session for a background loop). Video-only stream
    // selection is applied after opening via SetStreamSelection.
    attrs->SetUINT32(MF_LOW_LATENCY, TRUE);

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
        return false; // no compatible decoder for this codec/container
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
    while (!m_stopRequested) {
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
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) continue;

        BYTE* data = nullptr;
        DWORD maxLen = 0, curLen = 0;
        if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) continue;

        if (m_width > 0 && m_height > 0 && m_ctx) {
            const UINT32 stride = m_width * 4;
            D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_NONE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));

            // RGB32 from Media Foundation is bottom-up scanline order by
            // convention in some codecs' output; MF's RGB32 media type is
            // defined top-down, so we can copy directly without flipping.
            ComPtr<ID2D1Bitmap1> newBitmap;
            if (SUCCEEDED(m_ctx->CreateBitmap(D2D1::SizeU(m_width, m_height), data, stride, props,
                                               &newBitmap))) {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_currentBitmap = newBitmap;
            }
        }

        buffer->Unlock();

        // Pace decoding to the source frame rate so we don't burn CPU
        // decoding frames far faster than they're displayed.
        Sleep(static_cast<DWORD>(m_frameDuration100ns / 10000));
    }
}

ComPtr<ID2D1Bitmap1> VideoBackground::CurrentFrame() {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    return m_currentBitmap;
}

void VideoBackground::Shutdown() {
    m_stopRequested = true;
    if (m_thread) {
        WaitForSingleObject(m_thread, 2000);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }
    m_reader.Reset();
    std::lock_guard<std::mutex> lock(m_frameMutex);
    m_currentBitmap.Reset();
}

} // namespace fcs::background
