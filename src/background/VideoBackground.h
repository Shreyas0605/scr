#pragma once
#include <d2d1_1.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <string>
#include <mutex>

namespace fcs::background {

using Microsoft::WRL::ComPtr;

// Decodes and loops a video file (MP4/MOV/AVI/WEBM, subject to codecs
// registered with Media Foundation on the host machine - H.264/HEVC/AV1
// ship with Windows 10/11, WEBM/VP9 requires the optional "Web Media
// Extensions" package) using an IMFSourceReader configured to output
// RGB32, decoding on a dedicated worker thread so frame reads never stall
// the render thread. Decoded frames are copied into a double-buffered
// ID2D1Bitmap the render thread samples from, giving tear-free playback
// without blocking Present().
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

    std::mutex m_frameMutex;
    ComPtr<ID2D1Bitmap1> m_currentBitmap;

    LONGLONG m_frameDuration100ns = 333333; // fallback ~30fps if source omits duration
};

} // namespace fcs::background
