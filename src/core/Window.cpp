#include "Window.h"

namespace fcs::core {

bool Window::s_classRegistered = false;
constexpr wchar_t Window::kClassName[];

void Window::RegisterWindowClass(HINSTANCE hInstance) {
    if (s_classRegistered) return;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &Window::StaticWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = nullptr; // cursor visibility managed explicitly per role
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;

    RegisterClassExW(&wc);
    s_classRegistered = true;
}

Window::~Window() {
    if (m_hwnd) {
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool Window::CreateFullScreen(HINSTANCE hInstance, const RECT& monitorRect, int monitorIndex) {
    m_role = WindowRole::FullScreenSaver;
    RegisterWindowClass(hInstance);

    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    const DWORD style = WS_POPUP | WS_VISIBLE;

    wchar_t title[64];
    swprintf_s(title, L"FlipClockScreensaver_%d", monitorIndex);

    m_hwnd = CreateWindowExW(exStyle, kClassName, title, style, monitorRect.left, monitorRect.top,
                              monitorRect.right - monitorRect.left,
                              monitorRect.bottom - monitorRect.top, nullptr, nullptr, hInstance, this);
    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    ShowCursor(FALSE);
    return true;
}

bool Window::CreatePreview(HINSTANCE hInstance, HWND parentPreviewHwnd) {
    m_role = WindowRole::Preview;
    RegisterWindowClass(hInstance);

    RECT parentRect{};
    GetClientRect(parentPreviewHwnd, &parentRect);

    const DWORD style = WS_CHILD | WS_VISIBLE;
    m_hwnd = CreateWindowExW(0, kClassName, L"FlipClockScreensaverPreview", style, 0, 0,
                              parentRect.right - parentRect.left, parentRect.bottom - parentRect.top,
                              parentPreviewHwnd, nullptr, hInstance, this);
    return m_hwnd != nullptr;
}

RECT Window::ClientRect() const {
    RECT r{};
    if (m_hwnd) GetClientRect(m_hwnd, &r);
    return r;
}

LRESULT CALLBACK Window::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->WndProc(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    bool handled = false;
    LRESULT result = 0;
    if (m_handler) {
        result = m_handler(hwnd, msg, wParam, lParam, handled);
    }
    if (handled) return result;

    switch (msg) {
        case WM_DESTROY:
            if (m_role == WindowRole::FullScreenSaver) ShowCursor(TRUE);
            return 0;
        case WM_ERASEBKGND:
            return 1; // we always paint the full frame ourselves via D2D
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

std::vector<RECT> EnumerateMonitorRects() {
    std::vector<RECT> rects;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hMon, HDC, LPRECT, LPARAM lParam) -> BOOL {
            auto* out = reinterpret_cast<std::vector<RECT>*>(lParam);
            MONITORINFO mi{};
            mi.cbSize = sizeof(MONITORINFO);
            if (GetMonitorInfoW(hMon, &mi)) {
                out->push_back(mi.rcMonitor);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&rects));
    return rects;
}

} // namespace fcs::core
