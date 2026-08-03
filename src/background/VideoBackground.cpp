#include "VideoBackground.h"
#include <mfapi.h>
#include <mferror.h>
#include <mfobjects.h>
#include <propvarutil.h>
#include <cstring>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "d3d11.lib")

namespace fcs::background {

bool VideoBackground::Open(ID2D1DeviceContext* ctx, ID3D11Device* d3dDevice, const std::wstring& path,
                            bool loop, bool muted) {
    Shutdown();
    m_ctx = ctx;
    m_loop = loop;
    m_muted = muted;
    m_d3dDevice = d3dDevice;

    // --- Attempt to enable hardware (DXVA) decode ---
    // Associating the source reader with our own D3D11 device lets a
    // hardware decoder (when available) decode AND color-convert entirely
    // on the GPU, handing us back D3D11 textures directly. This is what
    // makes "any quality" video smooth - without it, every video decodes
    // in software regardless of what GPU is in the machine. If any step
    // here fails (older Windows, no DXVA driver support, etc.), we simply
    // don't set the D3D manager attribute and fall back to the software
    // decode path per-sample in DecodeLoop - playback still works, just
    // slower.
    if (m_d3dDevice) {
        m_d3dDevice->GetImmediateContext(&m_d3dContext);
        if (SUCCEEDED(MFCreateDXGIDeviceManager(&m_resetToken, &m_deviceManager)) && m_deviceManager) {
            m_hardwareDecodeAvailable =
                SUCCEEDED(m_deviceManager->ResetDevice(m_d3dDevice.Get(), m_resetToken));
        }
        if (!m_hardwareDecodeAvailable) {
            m_deviceManager.Reset();
            m_d3dContext.Reset();
        }
    }

    ComPtr<IMFAttributes> attrs;
    HRESULT hr = MFCreateAttributes(&attrs, 4);
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

    if (m_hardwareDecodeAvailable) {
        // Ties the reader to our D3D11 device so a hardware decoder MFT
        // can be selected, and lets the (GPU-based, when available) video
        // processor MFT perform the NV12->RGB32 conversion below without a
        // CPU round trip.
        attrs->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_deviceManager.Get());
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    }

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
    outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
    hr = m_reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                        nullptr, outputType.Get());
    if (FAILED(hr) && m_hardwareDecodeAvailable) {
        // Some hardware decoder/driver combinations don't support the
        // advanced-video-processing negotiation; retry once with hardware
        // decode disabled entirely rather than failing to open the video.
        m_hardwareDecodeAvailable = false;
        m_deviceManager.Reset();
        m_d3dContext.Reset();

        ComPtr<IMFAttributes> retryAttrs;
        if (FAILED(MFCreateAttributes(&retryAttrs, 2))) return false;
        retryAttrs->SetUINT32(MF_LOW_LATENCY, TRUE);
        retryAttrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

        m_reader.Reset();
        hr = MFCreateSourceReaderFromURL(path.c_str(), retryAttrs.Get(), &m_reader);
        if (FAILED(hr) || !m_reader) return false;
        m_reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        m_reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
        if (!m_muted) {
            m_reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);
        }
        hr = m_reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                            nullptr, outputType.Get());
    }
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

namespace {
// (Re)creates `tex` as a GPU-only texture matching `desc`'s size/format if
// it doesn't already match, so CopySubresourceRegion has a valid target.
bool EnsureCopyTarget(ID3D11Device* device, ComPtr<ID3D11Texture2D>& tex,
                       const D3D11_TEXTURE2D_DESC& srcDesc) {
    if (tex) {
        D3D11_TEXTURE2D_DESC existing{};
        tex->GetDesc(&existing);
        if (existing.Width == srcDesc.Width && existing.Height == srcDesc.Height &&
            existing.Format == srcDesc.Format) {
            return true;
        }
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = srcDesc.Width;
    desc.Height = srcDesc.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = srcDesc.Format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tex.Reset();
    return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &tex));
}
} // namespace

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
        if (FAILED(sample->GetBufferByIndex(0, &buffer)) || !buffer) continue;

        bool handledOnGpu = false;

        // --- Try the hardware (GPU-to-GPU) path first ---
        if (m_hardwareDecodeAvailable && m_d3dContext) {
            ComPtr<IMFDXGIBuffer> dxgiBuffer;
            if (SUCCEEDED(buffer.As(&dxgiBuffer)) && dxgiBuffer) {
                ComPtr<ID3D11Texture2D> srcTex;
                UINT subresource = 0;
                if (SUCCEEDED(dxgiBuffer->GetResource(IID_PPV_ARGS(&srcTex))) &&
                    SUCCEEDED(dxgiBuffer->GetSubresourceIndex(&subresource))) {
                    D3D11_TEXTURE2D_DESC srcDesc{};
                    srcTex->GetDesc(&srcDesc);

                    const int writeIdx = m_gpuWriteIndex;
                    if (EnsureCopyTarget(m_d3dDevice.Get(), m_gpuFrameTex[writeIdx], srcDesc)) {
                        m_d3dContext->CopySubresourceRegion(m_gpuFrameTex[writeIdx].Get(), 0, 0, 0, 0,
                                                             srcTex.Get(), subresource, nullptr);
                        {
                            std::lock_guard<std::mutex> lock(m_frameMutex);
                            m_gpuReadyIndex = writeIdx;
                            m_gpuFrameFormat = srcDesc.Format;
                            m_hasPendingGpuFrame = true;
                        }
                        m_gpuWriteIndex = 1 - writeIdx; // alternate buffers next frame
                        handledOnGpu = true;
                    }
                }
            }
        }

        // --- Software fallback: read the frame back to a CPU buffer ---
        if (!handledOnGpu) {
            ComPtr<IMFMediaBuffer> cpuBuffer;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&cpuBuffer)) && cpuBuffer) {
                BYTE* data = nullptr;
                DWORD maxLen = 0, curLen = 0;
                if (SUCCEEDED(cpuBuffer->Lock(&data, &maxLen, &curLen))) {
                    if (m_width > 0 && m_height > 0) {
                        const UINT32 stride = m_width * 4;
                        const size_t byteCount = static_cast<size_t>(stride) * m_height;

                        // Guard against a short buffer (shouldn't happen for
                        // RGB32 at this resolution, but a malformed/
                        // truncated source could produce one).
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
                    cpuBuffer->Unlock();
                }
            }
        }

        // Pace decoding to the source frame rate so we don't burn CPU/GPU
        // decoding frames far faster than they're displayed.
        Sleep(static_cast<DWORD>(m_frameDuration100ns / 10000));
    }
}

ComPtr<ID2D1Bitmap1> VideoBackground::CurrentFrame() {
    return m_currentBitmap;
}

void VideoBackground::SyncFrame(ID2D1DeviceContext* ctx) {
    if (!ctx) return;

    // --- Hardware path: wrap the freshly-copied GPU texture directly ---
    // CreateBitmapFromDxgiSurface does not copy pixel data - it just wraps
    // the existing texture memory, so this is cheap to call every time a
    // new frame is ready (no CPU involvement at all for this whole path).
    bool gpuFramePending = false;
    int readyIdx = -1;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (m_hasPendingGpuFrame) {
            gpuFramePending = true;
            readyIdx = m_gpuReadyIndex;
            m_hasPendingGpuFrame = false;
        }
    }
    if (gpuFramePending && readyIdx >= 0 && m_gpuFrameTex[readyIdx]) {
        ComPtr<IDXGISurface> surface;
        if (SUCCEEDED(m_gpuFrameTex[readyIdx].As(&surface)) && surface) {
            D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_NONE,
                D2D1::PixelFormat(m_gpuFrameFormat, D2D1_ALPHA_MODE_IGNORE));
            ComPtr<ID2D1Bitmap1> newBitmap;
            if (SUCCEEDED(ctx->CreateBitmapFromDxgiSurface(surface.Get(), &props, &newBitmap))) {
                m_currentBitmap = newBitmap;
                return;
            }
        }
    }

    // --- Software fallback path ---
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
    // Only valid when the existing bitmap isn't itself a DXGI-surface
    // wrapper from the hardware path (that path never reaches here anyway
    // once hardware decode is active for this stream, so this is safe).
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
    m_deviceManager.Reset();
    m_d3dContext.Reset();
    m_d3dDevice.Reset();
    m_hardwareDecodeAvailable = false;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_pendingPixels.clear();
        m_hasPendingFrame = false;
        m_hasPendingGpuFrame = false;
        m_gpuReadyIndex = -1;
    }
    m_gpuFrameTex[0].Reset();
    m_gpuFrameTex[1].Reset();
    m_currentBitmap.Reset(); // only ever touched on the main thread, safe without the lock
}

} // namespace fcs::background
