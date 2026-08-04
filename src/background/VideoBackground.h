#pragma once
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <string>
#include <mutex>
#include <vector>

namespace fcs::background {

using Microsoft::WRL::ComPtr;

// Decodes and loops a video file (MP4/MOV/AVI/WEBM, subject to codecs
// registered with Media Foundation on the host machine - H.264/HEVC/AV1
// ship with Windows 10/11, WEBM/VP9 requires the optional "Web Media
// Extensions" package) using an IMFSourceReader, decoding on a dedicated
// worker thread so frame reads never stall the render thread.
//
// Two decode paths, chosen automatically per-sample:
//   - Hardware (preferred): the reader is associated with the app's D3D11
//     device via IMFDXGIDeviceManager, so a hardware (DXVA) decoder can
//     decode AND color-convert entirely on the GPU. Each decoded frame
//     arrives as a D3D11 texture, which is copied (GPU-to-GPU, no CPU
//     round trip) into our own double-buffered texture via
//     CopySubresourceRegion. This is what makes high-resolution/high-
//     bitrate video playback smooth - software decoding of "any quality"
//     video simply can't keep up in real time on most hardware.
//   - Software (fallback): if a given sample isn't backed by a D3D11
//     texture (older codec, no hardware decoder registered, or a system
//     where DXVA isn't available), the frame is read back as a plain CPU
//     buffer and copied into m_pendingPixels, exactly as before. This path
//     is slower but keeps video playback working everywhere.
//
// In both cases, the decode thread only ever touches plain CPU memory or
// makes ID3D11DeviceContext calls (safe across threads because
// D2DRenderer marks its D3D11 context multithread-protected). It never
// calls into Direct2D directly. SyncFrame(), called once per frame from
// the render thread, is what actually creates/updates the ID2D1Bitmap the
// render thread draws - required because D2DRenderer's D2D factory is
// D2D1_FACTORY_TYPE_SINGLE_THREADED, so every ID2D1DeviceContext call must
// come from one consistent thread or it silently fails.
class VideoBackground {
public:
    ~VideoBackground() { Shutdown(); }

    // Opens `path` and starts the decode thread. `d3dDevice` (from
    // D2DRenderer::D3DDevice()) is used to attempt hardware-accelerated
    // decode; if that setup fails for any reason, playback still works via
    // the software fallback path. Returns false if the file can't be
    // opened or no compatible decoder is registered at all; caller should
    // fall back to a solid color / static frame in that case.
    bool Open(ID2D1DeviceContext* ctx, ID3D11Device* d3dDevice, const std::wstring& path, bool loop,
              bool muted);

    void Shutdown();

    // Returns the most recently decoded frame as a D2D bitmap ready to
    // draw, or nullptr if no frame has decoded yet. Safe to call from the
    // render thread every frame; internally just returns a cached pointer,
    // no decode work happens on this thread.
    ComPtr<ID2D1Bitmap1> CurrentFrame();

    // Must be called once per frame from the same thread that owns `ctx`
    // (the main render thread) before CurrentFrame() is drawn. This is
    // where the actual ID2D1Bitmap create/update happens - see the class
    // comment for why that can't safely happen on the decode thread.
    void SyncFrame(ID2D1DeviceContext* ctx);

    bool IsOpen() const { return m_reader != nullptr; }
    UINT32 FrameWidth() const { return m_width; }
    UINT32 FrameHeight() const { return m_height; }

private:
    static DWORD WINAPI DecodeThreadProc(LPVOID param);
    void DecodeLoop();

    ID2D1DeviceContext* m_ctx = nullptr;
    ComPtr<IMFSourceReader> m_reader;
    UINT32 m_width = 0;
    UINT32 m_height = 0;
    bool m_loop = true;
    bool m_muted = true;

    HANDLE m_thread = nullptr;
    volatile bool m_stopRequested = false;

    // --- Hardware decode path ---
    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<ID3D11DeviceContext> m_d3dContext; // same immediate context as D2DRenderer; multithread-protected
    ComPtr<IMFDXGIDeviceManager> m_deviceManager;
    UINT m_resetToken = 0;
    bool m_hardwareDecodeAvailable = false;
    volatile bool m_hardwarePathFailed = false; // set by SyncFrame() if wrapping a GPU frame ever fails

    // Double-buffered so the render thread can safely wrap "the other"
    // texture in a D2D bitmap while the decode thread copies the next
    // frame into the one not currently referenced.
    ComPtr<ID3D11Texture2D> m_gpuFrameTex[2];
    DXGI_FORMAT m_gpuFrameFormat = DXGI_FORMAT_UNKNOWN;
    int m_gpuWriteIndex = 0;
    int m_gpuReadyIndex = -1;
    bool m_hasPendingGpuFrame = false;

    // --- Software fallback path ---
    // Decode thread writes into these (raw CPU bytes only); SyncFrame() on
    // the main thread reads them.
    std::vector<BYTE> m_pendingPixels;
    UINT32 m_pendingWidth = 0;
    UINT32 m_pendingHeight = 0;
    UINT32 m_pendingStride = 0;
    bool m_hasPendingFrame = false;

    // Guards every "pending" field above (both paths) - a lightweight flag/
    // index swap, never held during the actual GPU copy or CPU memcpy.
    std::mutex m_frameMutex;

    ComPtr<ID2D1Bitmap1> m_currentBitmap; // only ever touched on the main thread

    LONGLONG m_frameDuration100ns = 333333; // fallback ~30fps if source omits duration
};

} // namespace fcs::background
