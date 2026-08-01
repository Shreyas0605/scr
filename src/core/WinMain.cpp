// Process entry point for FlipClock.scr.
//
// Windows invokes screensaver executables with a specific, historically
// quirky calling convention (see ParseCommandLine in Application.cpp for
// the full contract). This file's only job is to establish DPI awareness
// before any window is created, hand the raw command line to the parser,
// and delegate everything else to Application::Run.
#include "Application.h"
#include <shellscalingapi.h>

#pragma comment(lib, "Shcore.lib")

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR cmdLine,
                       int /*nCmdShow*/) {
    // Per-monitor-v2 DPI awareness must be set before any HWND is created
    // so that mixed-DPI multi-monitor setups (a 4K primary next to a 1080p
    // secondary, for example) each receive correctly-scaled WM_DPICHANGED
    // notifications rather than being bitmap-stretched by DWM.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const fcs::core::ParsedCommandLine args = fcs::core::ParseCommandLine(cmdLine);

    fcs::core::Application app;
    return app.Run(hInstance, args);
}
