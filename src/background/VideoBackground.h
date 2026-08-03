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
// the render thread. The decode thread only ever touches plain CPU memory;
// it never calls into Direct2D. SyncFrame(), called once per frame from the
// render thread, is what actually creates/updates the ID2D1Bitmap the
// render thread then draws - required because D2DRenderer's factory is
// D2D1_FACTORY_TYPE_SINGLE_THREADED, so every D2D call must come from one
// consistent thread or it silently fails (this is what previously made the
// video background render solid black: CreateBitmap was being called from
// the decode thread).
class VideoBackground {
public:
    ~VideoBackground() { Shutdown(); }

    // Opens `path` and starts the decode thread. Returns false if the file
    // can't be opened or no compatible decoder is registered; caller
    // should fall back to a solid color / static frame in that case.
    bool Open(ID2D1DeviceContext* ctx, const std::wstring& path, bool loop, bool muted);

    void Shutdown();

    // Returns the most recently decoded frame as a D2D bitmap ready to
    // draw, or nullptr if no frame has decoded yet. Safe to call from the
    // render thread every frame; internally just swaps a pointer under a
    // lightweight lock, no decode work happens on this thread.
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

    // Decode thread writes into these (raw CPU bytes only - no D2D calls);
    // SyncFrame() on the main thread reads them and produces m_currentBitmap.
    // D2D1_FACTORY_TYPE_SINGLE_THREADED (see D2DRenderer.cpp) means every
    // ID2D1DeviceContext call must come from one consistent thread, so the
    // decode thread must never touch `ctx` directly.
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
