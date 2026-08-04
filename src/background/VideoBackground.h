#pragma once
#include <d2d1_1.h>
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
// Extensions" package) using an IMFSourceReader configured to output
// RGB32, decoding on a dedicated worker thread so frame reads never stall
// the render thread.
//
// This is a pure software (CPU) decode pipeline: the decode thread reads
// each frame back as a plain CPU buffer and copies it into
// m_pendingPixels. It's deliberately simple and has been confirmed working
// across multiple builds - an earlier attempt at hardware-accelerated
// (DXVA/D3D11) decode introduced real regressions that couldn't be
// verified without access to real Windows/DirectX hardware, so that path
// was reverted in favor of reliability. Playback speed for very high
// resolution/bitrate video is limited by CPU decode throughput as a
// result; moderate resolution/bitrate (e.g. 1080p) plays smoothly.
//
// The decode thread only ever touches plain CPU memory - it never calls
// into Direct2D. SyncFrame(), called once per frame from the render
// thread, is what actually creates/updates the ID2D1Bitmap the render
// thread draws - required because D2DRenderer's D2D factory is
// D2D1_FACTORY_TYPE_SINGLE_THREADED, so every ID2D1DeviceContext call must
// come from one consistent thread or it silently fails.
class VideoBackground {
public:
    ~VideoBackground() { Shutdown(); }

    bool Open(ID2D1DeviceContext* ctx, const std::wstring& path, bool loop, bool muted);
    void Shutdown();

    // Returns the most recently decoded frame as a D2D bitmap ready to
    // draw, or nullptr if no frame has decoded yet.
    ComPtr<ID2D1Bitmap1> CurrentFrame();

    // Must be called once per frame from the same thread that owns `ctx`
    // (the main render thread) before CurrentFrame() is drawn.
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

    // Decode thread writes into these (raw CPU bytes only - no D2D calls);
    // SyncFrame() on the main thread reads them and produces m_currentBitmap.
    std::mutex m_frameMutex;
    std::vector<BYTE> m_pendingPixels;
    UINT32 m_pendingWidth = 0;
    UINT32 m_pendingHeight = 0;
    UINT32 m_pendingStride = 0;
    bool m_hasPendingFrame = false;

    ComPtr<ID2D1Bitmap1> m_currentBitmap; // only ever touched on the main thread

    LONGLONG m_frameDuration100ns = 333333; // fallback ~30fps if source omits duration
};

} // namespace fcs::background
