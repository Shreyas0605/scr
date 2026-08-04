#include "Application.h"
#include "../settings/SettingsDialog.h"
#include <shellscalingapi.h>
#include <windowsx.h>
#include <cwctype>
#include <algorithm>

#pragma comment(lib, "Shcore.lib")

namespace fcs::core {

namespace {
fcs::clock::TileStyle TileStyleFromSettings(const fcs::config::ThemeSetting& theme) {
    fcs::clock::TileStyle style;
    style.faceColor = D2D1::ColorF(theme.tileFace.r, theme.tileFace.g, theme.tileFace.b, theme.tileFace.a);
    style.faceColorAlt =
        D2D1::ColorF(theme.tileFaceAlt.r, theme.tileFaceAlt.g, theme.tileFaceAlt.b, theme.tileFaceAlt.a);
    style.digitColor =
        D2D1::ColorF(theme.digitColor.r, theme.digitColor.g, theme.digitColor.b, theme.digitColor.a);
    style.cornerRadius = theme.cornerRadius;
    style.fontFamily = theme.fontFamily;
    return style;
}

fcs::clock::ClockOptions ClockOptionsFromSettings(const fcs::config::ClockSettings& c) {
    fcs::clock::ClockOptions opts;
    opts.hourFormat =
        c.hourFormat == fcs::config::HourFormatSetting::H12 ? fcs::clock::HourFormat::H12 : fcs::clock::HourFormat::H24;
    opts.showSeconds = c.showSeconds;
    opts.showDate = c.showDate;
    opts.showWeekday = c.showWeekday;
    opts.localeAware = c.localeAware;
    opts.timezoneName = c.timezoneKey;
    opts.flipDurationSeconds = c.flipDurationSeconds;
    return opts;
}
} // namespace

ParsedCommandLine ParseCommandLine(PWSTR cmdLine) {
    ParsedCommandLine result;
    if (!cmdLine || !*cmdLine) {
        result.mode = RunMode::ScreenSaver; // bare invocation (e.g. double-click) still runs
        return result;
    }

    std::wstring cmd(cmdLine);
    // Trim leading whitespace.
    size_t i = 0;
    while (i < cmd.size() && iswspace(cmd[i])) ++i;
    cmd = cmd.substr(i);

    auto startsWithFlag = [&](const wchar_t* flag) {
        return _wcsnicmp(cmd.c_str(), flag, wcslen(flag)) == 0;
    };

    if (cmd.empty()) {
        result.mode = RunMode::ScreenSaver;
    } else if (startsWithFlag(L"/s")) {
        result.mode = RunMode::ScreenSaver;
    } else if (startsWithFlag(L"/p") || startsWithFlag(L"-p")) {
        result.mode = RunMode::Preview;
        // Format is "/p <hwnd>" or "/p<hwnd>" or "/p:<hwnd>" depending on
        // the caller (Display Settings uses "/p <hwnd>").
        size_t pos = cmd.find_first_of(L" :", 2);
        std::wstring hwndStr = pos != std::wstring::npos ? cmd.substr(pos + 1) : cmd.substr(2);
        result.previewParent = reinterpret_cast<HWND>(static_cast<intptr_t>(_wtoi64(hwndStr.c_str())));
    } else if (startsWithFlag(L"/c")) {
        result.mode = RunMode::Configure;
    } else if (startsWithFlag(L"/a")) {
        result.mode = RunMode::Password;
    } else {
        // Unknown/legacy flag: default to screensaver mode rather than
        // refusing to run, matching typical .scr host behavior.
        result.mode = RunMode::ScreenSaver;
    }
    return result;
}

void Application::InitializeSurface(MonitorSurface& surface, HWND hwnd, UINT w, UINT h, float dpi) {
    surface.renderer = std::make_unique<fcs::graphics::D2DRenderer>();
    surface.renderer->Initialize(hwnd, w, h, dpi);

    surface.clock = std::make_unique<fcs::clock::FlipClock>();
    surface.clock->Initialize(surface.renderer->Context(), surface.renderer->DWriteFactory(),
                               TileStyleFromSettings(m_settings.theme), ClockOptionsFromSettings(m_settings.clock));

    surface.background = std::make_unique<fcs::background::BackgroundManager>();
    surface.background->Initialize(surface.renderer->Context(), surface.renderer->WicFactory());
    surface.background->ApplySettings(m_settings.background);
}

bool Application::InitializeFullScreen(HINSTANCE hInstance) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const std::vector<RECT> monitors = EnumerateMonitorRects();
    if (monitors.empty()) return false;

    int index = 0;
    for (const RECT& rect : monitors) {
        auto surface = std::make_unique<MonitorSurface>();
        surface->window = std::make_unique<Window>();
        if (!surface->window->CreateFullScreen(hInstance, rect, index)) continue;

        surface->window->SetMessageHandler(
            [this](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& handled) {
                return HandleMessage(hwnd, msg, wParam, lParam, handled);
            });

        const UINT dpi = GetDpiForWindow(surface->window->Handle());
        const RECT client = surface->window->ClientRect();
        InitializeSurface(*surface, surface->window->Handle(),
                           static_cast<UINT>(client.right - client.left),
                           static_cast<UINT>(client.bottom - client.top), static_cast<float>(dpi));

        m_surfaces.push_back(std::move(surface));
        ++index;

        // In "Mirror" mode every monitor shows an identical clock, so one
        // fullscreen window per monitor is created but they all render
        // from the same settings snapshot (already the default here).
        // "Independent" mode would let each monitor carry its own
        // BackgroundSettings/timezone; the settings schema already
        // supports per-instance overrides at the MonitorSurface level for
        // that extension point.
        if (m_settings.monitor.mode == fcs::config::MonitorSettings::MultiMonitorMode::Span) {
            break; // span mode uses a single window covering the virtual desktop instead
        }
    }

    if (m_settings.monitor.mode == fcs::config::MonitorSettings::MultiMonitorMode::Span &&
        monitors.size() > 1) {
        // Recreate as a single window spanning the union of all monitor
        // rects rather than one window per monitor.
        m_surfaces.clear();
        RECT unionRect = monitors[0];
        for (const RECT& r : monitors) UnionRect(&unionRect, &unionRect, &r);

        auto surface = std::make_unique<MonitorSurface>();
        surface->window = std::make_unique<Window>();
        if (surface->window->CreateFullScreen(hInstance, unionRect, 0)) {
            surface->window->SetMessageHandler(
                [this](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& handled) {
                    return HandleMessage(hwnd, msg, wParam, lParam, handled);
                });
            const UINT dpi = GetDpiForWindow(surface->window->Handle());
            const RECT client = surface->window->ClientRect();
            InitializeSurface(*surface, surface->window->Handle(),
                               static_cast<UINT>(client.right - client.left),
                               static_cast<UINT>(client.bottom - client.top), static_cast<float>(dpi));
            m_surfaces.push_back(std::move(surface));
        }
    }

    return !m_surfaces.empty();
}

bool Application::InitializePreview(HINSTANCE hInstance, HWND previewParent) {
    if (!previewParent || !IsWindow(previewParent)) return false;

    auto surface = std::make_unique<MonitorSurface>();
    surface->window = std::make_unique<Window>();
    if (!surface->window->CreatePreview(hInstance, previewParent)) return false;

    const UINT dpi = GetDpiForWindow(previewParent);
    const RECT client = surface->window->ClientRect();
    InitializeSurface(*surface, surface->window->Handle(),
                       static_cast<UINT>(std::max<LONG>(1, client.right - client.left)),
                       static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top)),
                       static_cast<float>(dpi));

    m_surfaces.push_back(std::move(surface));
    return true;
}

LRESULT Application::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& handled) {
    if (m_mode != RunMode::ScreenSaver) {
        handled = false;
        return 0;
    }

    switch (msg) {
        case WM_MOUSEMOVE: {
            POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(hwnd, &p);
            if (!m_mouseInitialized) {
                m_lastMousePos = p;
                m_mouseInitialized = true;
            } else {
                const int dx = p.x - m_lastMousePos.x;
                const int dy = p.y - m_lastMousePos.y;
                if ((dx * dx + dy * dy) > (kMouseMoveExitThresholdPixels * kMouseMoveExitThresholdPixels)) {
                    RequestExit();
                }
            }
            handled = true;
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            RequestExit();
            handled = true;
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) RequestExit();
            handled = true;
            return 0;
        case WM_CLOSE:
        case WM_DESTROY:
            RequestExit();
            handled = false; // let default handling also run (DestroyWindow etc.)
            return 0;
        default:
            handled = false;
            return 0;
    }
}

void Application::RequestExit() { m_exitRequested = true; }

void Application::RenderFrame(MonitorSurface& surface) {
    if (surface.renderer->IsDeviceLost()) {
        // Full device-lost recovery: tear down and reinitialize GPU
        // resources against a fresh D3D device (e.g. after a driver
        // reset), then skip this frame.
        const RECT client = surface.window->ClientRect();
        surface.renderer->Shutdown();
        surface.renderer->Initialize(surface.window->Handle(),
                                      static_cast<UINT>(client.right - client.left),
                                      static_cast<UINT>(client.bottom - client.top), surface.renderer->Dpi());
        surface.clock->Initialize(surface.renderer->Context(), surface.renderer->DWriteFactory(),
                                   TileStyleFromSettings(m_settings.theme),
                                   ClockOptionsFromSettings(m_settings.clock));
        surface.background->Initialize(surface.renderer->Context(), surface.renderer->WicFactory());
        surface.background->ApplySettings(m_settings.background);
        return;
    }

    surface.clock->Update();
    surface.background->Update();

    surface.renderer->BeginDraw();
    // IMPORTANT: use the DIP-based logical size, not the swap chain's pixel
    // Width()/Height(). Direct2D draws in DIPs; feeding it pixel counts on
    // any display scaled above 100% makes the background/clock draw into a
    // region larger than the actual framebuffer, clipping the right/bottom
    // edge and shifting/shrinking the tile layout - exactly the corruption
    // reported on real hardware. See D2DRenderer::LogicalSize().
    const D2D1_SIZE_F logicalSize = surface.renderer->LogicalSize();
    const D2D1_RECT_F viewport = D2D1::RectF(0.0f, 0.0f, logicalSize.width, logicalSize.height);
    surface.background->Draw(viewport);
    surface.clock->Draw(viewport);
    surface.renderer->EndDraw(m_settings.performance.vsync);
}

int Application::Run(HINSTANCE hInstance, const ParsedCommandLine& args) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION);

    m_settings = fcs::config::LoadSettings();
    m_mode = args.mode;

    // /a (legacy password-change contract): modern Windows resumes from the
    // lock screen itself, so we simply exit successfully without showing UI.
    if (args.mode == RunMode::Password) {
        MFShutdown();
        CoUninitialize();
        return 0;
    }

    // /c (Configure): show the modal settings dialog and exit; never enters
    // the render loop at all.
    if (args.mode == RunMode::Configure) {
        fcs::settings::ShowSettingsDialog(hInstance, args.previewParent, m_settings);
        MFShutdown();
        CoUninitialize();
        return 0;
    }

    bool ok = false;
    if (args.mode == RunMode::Preview) {
        ok = InitializePreview(hInstance, args.previewParent);
    } else {
        // ScreenSaver and Unknown (bare invocation with no recognized flag)
        // both fall back to the standard fullscreen screensaver behavior.
        ok = InitializeFullScreen(hInstance);
    }
    if (!ok) {
        MFShutdown();
        CoUninitialize();
        return 1;
    }

    // Render/message pump: PeekMessage so we can drive the frame loop at a
    // steady cadence instead of blocking on GetMessage. In preview mode
    // the host embeds our HWND, so we're one of potentially many windows
    // being pumped by *its* message loop's paint cycle in practice, but we
    // still run our own tiny loop here since the .scr contract runs as an
    // independent process either way.
    const double targetFrameSeconds = 1.0 / std::max(30, m_settings.performance.targetFps);
    m_clock.Reset();

    MSG msg{};
    while (!m_exitRequested) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_exitRequested = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (m_exitRequested) break;

        for (auto& surface : m_surfaces) {
            const bool visible = IsWindowVisible(surface->window->Handle()) != 0;
            if (!visible && m_settings.performance.sleepWhenHidden) continue;
            RenderFrame(*surface);
        }

        // Frame pacing fallback for the (rare) case vsync is disabled or a
        // WARP software device can't hit real-time; avoids pegging a core.
        if (!m_settings.performance.vsync) {
            const double elapsed = m_clock.Tick();
            const double remaining = targetFrameSeconds - elapsed;
            if (remaining > 0.001) {
                Sleep(static_cast<DWORD>(remaining * 1000.0));
            }
        } else {
            m_clock.Tick();
        }
    }

    m_surfaces.clear();
    MFShutdown();
    CoUninitialize();
    return 0;
}

} // namespace fcs::core
