#pragma once
#include <windows.h>
#include <functional>
#include <vector>

namespace fcs::core {

enum class WindowRole {
    FullScreenSaver, // /s : one borderless topmost window per monitor
    Preview,          // /p <HWND> : child window embedded in the little
                      //             thumbnail box in Display Settings
};

// Thin RAII wrapper around a Win32 HWND configured appropriately for each
// screensaver role. Fullscreen mode creates a topmost, borderless,
// cursor-hidden window covering one monitor's bounds; preview mode creates
// a child window parented to the HWND the OS passes via `/p <hwnd>`.
class Window {
public:
    using MessageHandler = std::function<LRESULT(HWND, UINT, WPARAM, LPARAM, bool& handled)>;

    Window() = default;
    ~Window();

    // Creates a fullscreen topmost window covering `monitorRect` (screen
    // coordinates). One Window is created per monitor when spanning
    // multiple displays.
    bool CreateFullScreen(HINSTANCE hInstance, const RECT& monitorRect, int monitorIndex);

    // Creates a child window embedded into `parentPreviewHwnd`, sized to
    // fill its client area, as required by the `/p` preview contract.
    bool CreatePreview(HINSTANCE hInstance, HWND parentPreviewHwnd);

    HWND Handle() const { return m_hwnd; }
    RECT ClientRect() const;
    WindowRole Role() const { return m_role; }

    void SetMessageHandler(MessageHandler handler) { m_handler = std::move(handler); }

    static void RegisterWindowClass(HINSTANCE hInstance);

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    WindowRole m_role = WindowRole::FullScreenSaver;
    MessageHandler m_handler;

    static constexpr wchar_t kClassName[] = L"FlipClockScreensaverWndClass";
    static bool s_classRegistered;
};

// Enumerates all active monitors' bounding rectangles, used both for
// spawning one fullscreen window per display and for the multi-monitor
// "span" mode which unions them into one virtual desktop rect.
std::vector<RECT> EnumerateMonitorRects();

} // namespace fcs::core
