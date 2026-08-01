#pragma once
#include <windows.h>
#include <memory>
#include <vector>
#include "Window.h"
#include "../graphics/D2DRenderer.h"
#include "../clock/FlipClock.h"
#include "../background/BackgroundManager.h"
#include "../config/Settings.h"
#include "../animation/AnimationClock.h"

namespace fcs::core {

enum class RunMode { ScreenSaver, Preview, Configure, Password, Unknown };

// Parses the Windows screensaver command line contract:
//   FlipClock.scr              -> ScreenSaver (some launchers omit the flag)
//   FlipClock.scr /s           -> ScreenSaver
//   FlipClock.scr /p <HWND>    -> Preview, embedded in the given window
//   FlipClock.scr /c           -> Configure (settings dialog)
//   FlipClock.scr /c:<HWND>    -> Configure, modal to the given parent
//   FlipClock.scr /a <HWND>    -> Password-change dialog (legacy contract;
//                                  modern Windows handles resume-password
//                                  via the lock screen, so we simply no-op
//                                  and exit successfully here)
struct ParsedCommandLine {
    RunMode mode = RunMode::Unknown;
    HWND previewParent = nullptr;
};

ParsedCommandLine ParseCommandLine(PWSTR cmdLine);

// One render surface + its owned subsystems, for one monitor (fullscreen
// mode) or the single preview thumbnail (preview mode).
struct MonitorSurface {
    std::unique_ptr<Window> window;
    std::unique_ptr<fcs::graphics::D2DRenderer> renderer;
    std::unique_ptr<fcs::clock::FlipClock> clock;
    std::unique_ptr<fcs::background::BackgroundManager> background;
};

// Top-level owner of the running screensaver instance: creates one
// MonitorSurface per display (or one embedded surface for preview mode),
// runs the frame loop, and implements the standard screensaver-exit
// contract (any keypress, meaningful mouse movement, or mouse click ends
// the screensaver in fullscreen mode).
class Application {
public:
    int Run(HINSTANCE hInstance, const ParsedCommandLine& args);

private:
    bool InitializeFullScreen(HINSTANCE hInstance);
    bool InitializePreview(HINSTANCE hInstance, HWND previewParent);
    void InitializeSurface(MonitorSurface& surface, HWND hwnd, UINT w, UINT h, float dpi);

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& handled);
    void RenderFrame(MonitorSurface& surface);
    void RequestExit();

    fcs::config::Settings m_settings;
    std::vector<std::unique_ptr<MonitorSurface>> m_surfaces;
    fcs::animation::AnimationClock m_clock;

    POINT m_lastMousePos{-1, -1};
    bool m_mouseInitialized = false;
    bool m_exitRequested = false;
    RunMode m_mode = RunMode::Unknown;

    static constexpr int kMouseMoveExitThresholdPixels = 8;
};

} // namespace fcs::core
