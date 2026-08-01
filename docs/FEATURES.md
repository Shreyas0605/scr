# Feature Reference

## Screensaver modes (standard Windows contract)

| Invocation | Behavior |
|---|---|
| `FlipClock.scr` (no args) | Runs fullscreen, same as `/s` |
| `FlipClock.scr /s` | Runs fullscreen on every monitor (per `Monitor > Mode`) |
| `FlipClock.scr /p <hwnd>` | Renders embedded/child inside the given window (Display Settings thumbnail) |
| `FlipClock.scr /c` or `/c:<hwnd>` | Opens the settings dialog, modal to `<hwnd>` if given |
| `FlipClock.scr /a <hwnd>` | Legacy password-change contract; exits immediately (modern Windows handles resume-password via the lock screen itself) |

Exiting fullscreen mode: any keypress, mouse click, or mouse movement past
an 8-pixel threshold (to avoid false positives from trackpad/mouse jitter)
ends the screensaver, matching standard Windows screensaver behavior.
Preview-mode windows never exit on input — they're a passive thumbnail.

## Clock display

- **Format**: 12-hour or 24-hour.
- **Seconds**: shown or hidden. When hidden, the seconds tiles and their
  colon separator are omitted from layout entirely (not just blanked), so
  the remaining HH:MM stays centered.
- **Date / weekday**: independently toggleable; rendered as a subdued line
  beneath the tiles.
- **Locale awareness**: date/weekday formatting follows the system locale
  when `localeAware` is on.
- **Time zone**: defaults to system local time; can be pinned to any zone
  registered on the machine (populated live from
  `EnumDynamicTimeZoneInformation`, so it always matches what's actually
  installed rather than a hardcoded list). Time zone math correctly
  accounts for DST rules for the current date via
  `GetTimeZoneInformationForYear`.
- **Flip speed**: 250-350ms, adjustable via the Clock tab's slider.

## Backgrounds

| Mode | Notes |
|---|---|
| Solid Color | Flat fill, any RGBA |
| Image | PNG / JPEG / WEBP / BMP / HEIF (whatever WIC codecs are present); scale modes: Fill, Fit, Stretch, Center, Tile |
| Slideshow | Point at a folder; images load automatically, optional shuffle, configurable interval and crossfade duration |
| Video | MP4 / MOV / AVI / WEBM via Media Foundation; looping, optional mute |
| Animated Gradient | Slowly-cycling multi-stop gradient, configurable cycle duration |

**Blur** and **Brightness** (0-100 sliders) apply to every mode via a real
`ID2D1Effect` chain (`CLSID_D2D1GaussianBlur` → a brightness transfer
effect) evaluated on the GPU every frame — not a screenshot-and-blur
approximation — so it stays correct as slideshow images crossfade or video
frames advance.

## Themes

Nine built-in presets (Appearance tab): **Minimal Black**, **AMOLED**,
**Glass**, **Dark Gray**, **Wood**, **Retro Flip**, **Airport Board**,
**Neon**, **Mechanical**. Each sets tile face/alt colors, digit color,
label color, background color, corner radius, and font family together;
selecting one applies all of them at once. Corner radius and font can be
further adjusted independently afterward.

## Multi-monitor

Three modes (Monitor tab isn't currently exposed in the settings UI, but
`MonitorSettings::MultiMonitorMode` supports all three at the config
level; see `docs/ARCHITECTURE.md` for the extension point):

- **Mirror** (default): one fullscreen window per monitor, each showing an
  identical live clock.
- **Independent**: same window-per-monitor structure; the schema supports
  giving each `MonitorSurface` its own background/timezone override.
- **Span**: one window covering the union of every monitor's bounds,
  useful for a single centered clock across an ultrawide or multi-monitor
  desktop treated as one canvas.

Mixed-DPI setups are handled via `WM_DPICHANGED` — each window's renderer
gets its own DPI and resizes its swap chain independently, so a 4K primary
next to a 1080p secondary each render crisply rather than one being
bitmap-stretched.

## Performance

- **VSync**: on by default (tear-free `FLIP_DISCARD` presentation).
- **Target FPS**: 30/60/75/120/144, used as a frame-pacing fallback when
  VSync is off.
- **Sleep when hidden**: skips rendering entirely for any surface whose
  window isn't currently visible (relevant mainly to the preview
  thumbnail scrolling off-screen in a long Settings list).
- **Device-lost recovery**: a lost D3D11 device (driver reset, GPU
  removal) triggers a full pipeline rebuild against a fresh device rather
  than attempting to patch up individual COM objects.

## Settings persistence

All settings live in one JSON file at
`%APPDATA%\FlipClockScreensaver\settings.json`, written via the vendored
`nlohmann::json`. Malformed or partially-missing JSON never crashes the
screensaver — any field that fails to parse falls back to its default
individually, so a corrupted file degrades gracefully instead of resetting
everything.
