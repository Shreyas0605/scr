# FlipClock Screensaver

A native Windows split-flap ("departure board") clock screensaver, built with
Direct2D, DirectWrite, and Media Foundation for GPU-accelerated rendering.

![status](https://img.shields.io/badge/status-buildable--needs--Windows--SDK-blue)

## Features

- Mechanical flip-tile animation (real hinge/perspective transform, not a crossfade) for hours/minutes/seconds
- Backgrounds: solid color, still image (PNG/JPEG/WEBP/BMP/HEIF), slideshow with crossfade, looping video, animated gradient
- Real-time GPU Gaussian blur and brightness controls over any background
- 9 built-in themes (Minimal Black, AMOLED, Glass, Dark Gray, Wood, Retro Flip, Airport Board, Neon, Mechanical)
- 12/24-hour format, optional date/weekday, locale- and timezone-aware
- Full multi-monitor support: mirror, independent, or span mode; mixed-DPI aware
- Standard Windows screensaver contract: `/s`, `/p <hwnd>`, `/c`, `/a`
- Native tabbed settings dialog with live JSON persistence

## Quick start

1. Open `FlipClockScreensaver.sln` in Visual Studio 2022 (Desktop development with C++ workload).
2. Select **Release | x64**, then **Build > Build Solution**.
3. The output is `bin\x64\Release\FlipClock.scr`.
4. Install it (see below), or right-click it in Explorer and choose **Install**.

## Installing

```powershell
# From an elevated PowerShell prompt, from the repo root:
.\installer\Install-FlipClockScreensaver.ps1 -SetActive
```

This copies `FlipClock.scr` into `%WINDIR%\System32` (where Windows looks for
screensavers) and optionally makes it the active one. To remove it:

```powershell
.\installer\Uninstall-FlipClockScreensaver.ps1
```

You can also just double-click `FlipClock.scr` (runs it fullscreen), or
right-click it for **Install** / **Test** / **Configure** — these are
provided automatically by Explorer for any `.scr` file once it correctly
handles the `/s /p /c` arguments, which this project does.

## Documentation

- [`docs/BUILD.md`](docs/BUILD.md) — prerequisites, build configurations, troubleshooting
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — module responsibilities and how they fit together
- [`docs/FEATURES.md`](docs/FEATURES.md) — full feature reference and how each is configured
- [`docs/FOLDERS.md`](docs/FOLDERS.md) — what lives where in the repo

## Requirements

- Windows 10 or 11
- Visual Studio 2022 with the "Desktop development with C++" workload
- Windows 10 SDK (installed with that workload)

## License

Copyright (C) 2026. All rights reserved.
