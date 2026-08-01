# Architecture

## Runtime flow

```
WinMain.cpp
  └─ SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)
  └─ ParseCommandLine(cmdLine)          [Application.cpp]
  └─ Application::Run(hInstance, args)  [Application.cpp]
        ├─ /a  → no-op, exit 0 (modern Windows handles resume-password itself)
        ├─ /c  → SettingsDialog::ShowSettingsDialog(...), exit 0
        ├─ /p  → InitializePreview(...) then enter the render loop
        └─ /s  → InitializeFullScreen(...) then enter the render loop
```

`Application` is the single top-level owner of the running instance. It
never talks to Direct2D, DirectWrite, or Media Foundation directly — it
owns one `MonitorSurface` per display (or one for the embedded preview),
and each `MonitorSurface` bundles the four subsystems that actually do the
work:

```
MonitorSurface
  ├─ Window            (core/)       — the HWND itself
  ├─ D2DRenderer        (graphics/)  — D3D11 device + swap chain + D2D context
  ├─ FlipClock          (clock/)     — the clock widget: tiles, labels, date line
  └─ BackgroundManager  (background/) — solid/image/slideshow/video/gradient + blur/brightness
```

Every frame, `Application::RenderFrame` calls, in order:
`background->Update()` → `clock->Update()` → `renderer->BeginDraw()` →
`background->Draw(viewport)` → `clock->Draw(viewport)` →
`renderer->EndDraw(vsync)`. The background draws first so the clock
composites on top of it.

## Module responsibilities

### `core/`
- **`Application`** — command-line dispatch, per-monitor surface lifecycle,
  the message/render loop, device-lost recovery, and the standard
  screensaver-exit contract (keypress/click/meaningful-mouse-move in
  fullscreen mode; nothing in preview mode).
- **`Window`** — a thin RAII wrapper around one `HWND`, configured either as
  a topmost borderless fullscreen window (`CreateFullScreen`) or a child
  window embedded in the Display Settings thumbnail (`CreatePreview`).
  Also owns `EnumerateMonitorRects()` for multi-monitor enumeration.
- **`TimeZoneUtil`** — wraps `EnumDynamicTimeZoneInformation` /
  `GetTimeZoneInformationForYear` so the clock and settings dialog can work
  with real per-machine time zone data (including DST rules for arbitrary
  years) instead of a hardcoded list.

### `graphics/`
- **`D2DRenderer`** — owns the D3D11 device, DXGI swap chain
  (`FLIP_DISCARD` for tear-free vsync), the D2D1 device context bound to
  the swap chain's back buffer, and the shared `IDWriteFactory` /
  `IWICImagingFactory`. This is the one GPU surface every other subsystem
  draws into. Chosen over the legacy `ID2D1HwndRenderTarget` specifically
  because real-time `ID2D1Effect` blur and mixed-DPI bitmap handling need
  the DXGI-backed device context.

### `clock/`
- **`FlipTile`** — one split-flap character. Owns its own animation state:
  `SetValue()` with a new character starts a ~250-350ms eased flip from the
  old value to the new one, modeled as two independently-transformed
  half-panels (front top/bottom + a mid-flip "flap" with a perspective
  transform simulating the mechanical hinge).
- **`FlipClock`** — the composed widget: six `FlipTile`s (HH MM SS, two
  digits each), colon separators, section labels, and the optional
  date/weekday line. Diffs the current wall-clock digits against the
  previous frame's so only tiles whose digit actually changed animate.

### `background/`
- **`BackgroundManager`** — mode dispatch (solid/image/slideshow/video/
  gradient) plus the shared `ID2D1Effect` chain
  (`CLSID_D2D1GaussianBlur` → a brightness transfer effect) applied to
  whichever mode is active, so blur/brightness are real per-pixel GPU
  effects rather than a pre-baked approximation.
- **`ImageLoader`** — WIC-based decode-to-`ID2D1Bitmap1` for stills and
  slideshow frames (PNG/JPEG/WEBP/BMP/HEIF, whatever WIC codecs are present
  on the machine).
- **`VideoBackground`** — Media Foundation source reader driving a looping
  video texture, decoded straight to a D2D-compatible surface.

### `animation/`
- **`AnimationClock`** — steady-clock-based timing (not `GetTickCount`, to
  avoid long-uptime drift) plus `Transition`, a small reusable "start at
  time T, run for duration D, report linear progress" helper used by both
  flip-tile animations and background crossfades.
- **`Easing.h`** — the actual easing curve(s) applied to that linear
  progress for the "luxurious" (not linear, not bouncy) motion feel.

### `config/`
- **`Settings`** — the full settings schema (theme, background, clock,
  monitor, performance) plus `LoadSettings()`/`SaveSettings()`, serialized
  to `%APPDATA%\FlipClockScreensaver\settings.json` via the vendored
  `nlohmann::json` (`json.hpp`). Invalid/missing JSON never throws — it
  falls back to `Settings::Default()` field-by-field.
- **`Themes`** — the 9 built-in `ThemeSetting` presets (colors, corner
  radius, font) that populate the Appearance tab's dropdown.

### `settings/`
- **`SettingsDialog`** — the `/c` configuration window. Built with plain
  `CreateWindowEx` calls (a `SysTabControl32` plus per-tab child controls
  shown/hidden on `TCN_SELCHANGE`) rather than an `.rc` `DIALOGEX`
  template, which keeps the whole dialog's logic and layout in one
  reviewable `.cpp` file. Edits a working copy of `Settings` and only
  calls `SaveSettings()`/writes back to the caller's reference on
  OK/Apply, so Cancel is a true no-op.

### `resources/`
- **`FlipClock.rc`** — the app icon, the embedded manifest, and version
  info. No dialog templates live here (see `settings/` above for why).
- **`resource.h`** — the one symbol (`IDI_APPICON`) shared between the
  `.rc` file and the C++ code that loads it.
- **`FlipClock.manifest`** — per-monitor-v2 DPI awareness declaration and
  the Common Controls v6 dependency (needed for themed buttons/combos/
  trackbars/tab control rather than classic unthemed Win32 widgets).

## Why Direct2D-over-DXGI instead of `ID2D1HwndRenderTarget`

The legacy hwnd render target is simpler to set up, but doesn't support
`ID2D1Effect` (so no real GPU blur), doesn't let you control the swap
effect (so no `FLIP_DISCARD` tear-free presentation), and handles
mixed-DPI/multi-monitor bitmap scaling less predictably. Every non-trivial
feature this project needs (real-time blur, tear-free vsync, mixed-DPI
correctness) pushed toward the DXGI-backed device context from the start,
so that's what `D2DRenderer` wraps.

## Why a hand-built settings dialog instead of a `DIALOGEX` resource

Either approach is valid Win32. A resource-template dialog keeps layout
declarative but splits the "what controls exist" description (the `.rc`
file) from "what they do" (the `.cpp` `DialogProc`) across two files with a
shared, easy-to-typo set of numeric IDs. Building it with `CreateWindowEx`
keeps ID definitions, layout, and behavior together in
`SettingsDialog.cpp`, at the cost of manually computing pixel positions
instead of using the resource editor's WYSIWYG designer. Given the number
of tabs and controls here, the trade favors having one coherent file to
read and modify.
