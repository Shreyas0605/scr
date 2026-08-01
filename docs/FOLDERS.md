# Folder Guide

```
FlipClockScreensaver/
├── FlipClockScreensaver.sln          Visual Studio 2022 solution
├── FlipClockScreensaver.vcxproj      The (single) project: all source, resource, and link settings
├── FlipClockScreensaver.vcxproj.filters   Solution Explorer folder groupings
├── README.md
│
├── src/
│   ├── core/            Application entry point, command-line parsing, window
│   │                    management, the render/message loop, time zone helpers
│   ├── graphics/         D2DRenderer: the D3D11/DXGI/D2D1 GPU pipeline shared
│   │                    by every other subsystem
│   ├── clock/            FlipClock + FlipTile: the split-flap widget itself
│   ├── background/       BackgroundManager, ImageLoader (WIC), VideoBackground
│   │                    (Media Foundation) — everything behind the clock
│   ├── animation/         AnimationClock + Easing: shared timing/easing used by
│   │                    both flip-tile animation and background crossfades
│   ├── config/           Settings schema + JSON load/save, and the built-in
│   │                    Themes preset table
│   └── settings/         The /c settings dialog (native Win32, tabbed)
│
├── resources/
│   ├── FlipClock.rc       Icon, embedded manifest, version info
│   ├── FlipClock.manifest DPI-awareness + Common Controls v6 declaration
│   └── resource.h         Shared symbol(s) between the .rc file and C++ code
│
├── assets/
│   ├── icons/app.ico      Application/taskbar icon
│   └── fonts/             Reserved for any bundled custom fonts (currently
│                          empty — the shipped themes all use fonts already
│                          present on Windows, e.g. Segoe UI, Consolas)
│
├── installer/
│   ├── Install-FlipClockScreensaver.ps1    Copies the built .scr into
│   │                                       %WINDIR%\System32 and optionally
│   │                                       sets it active
│   └── Uninstall-FlipClockScreensaver.ps1  Reverses the above
│
└── docs/
    ├── BUILD.md           Prerequisites, build steps, troubleshooting
    ├── ARCHITECTURE.md     Module responsibilities and runtime flow
    ├── FEATURES.md         Full feature reference
    └── FOLDERS.md          This file
```

## Build output (created by Visual Studio, not checked in)

```
bin/x64/Debug|Release/FlipClock.scr    The built screensaver
obj/x64/Debug|Release/.../             Intermediate object files
```
