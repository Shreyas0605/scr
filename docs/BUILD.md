# Build Guide

## Prerequisites

- **Visual Studio 2022** (any edition, including Community) with the
  **"Desktop development with C++"** workload installed. This pulls in:
  - MSVC v143 toolset
  - Windows 10 SDK (any recent version; the project targets `10.0` generically)
  - C++ CMake tools (not required by this project, but harmless if present)
- Windows 10 or 11 to actually run the built `.scr` (Direct2D 1.1 / D3D11 /
  Media Foundation are all Windows-only APIs; this cannot be cross-compiled
  or run on Linux/macOS).

No external package manager (vcpkg, NuGet, Conan) is required — every
dependency (`d2d1`, `dwrite`, `d3d11`, `dxgi`, `wincodec`/WIC, Media
Foundation, Common Controls) ships as part of the Windows SDK, and the JSON
library (`nlohmann::json`, single-header `src/config/json.hpp`) is vendored
directly in the repo.

## Building in Visual Studio

1. Open `FlipClockScreensaver.sln`.
2. Choose a configuration/platform: **Debug|x64** or **Release|x64** (top toolbar).
3. **Build > Build Solution** (Ctrl+Shift+B).
4. Output lands in `bin\x64\<Configuration>\FlipClock.scr`, with intermediate
   object files in `obj\x64\<Configuration>\FlipClockScreensaver\`.

### Testing without installing

- **Preview thumbnail behavior**: right-click `FlipClock.scr` in Explorer and
  choose **Test** — this launches it with `/s` (fullscreen), same as
  Windows would when the screensaver timer fires.
- **Settings dialog**: right-click and choose **Configure**, or run
  `FlipClock.scr /c` from a command prompt.
- **Embedded preview** (the exact small-thumbnail mode Windows uses inside
  Settings > Personalization > Lock screen > Screen saver settings): this
  can't be triggered standalone without a host `HWND`, since `/p` expects a
  window handle argument from the caller. The easiest way to test it is to
  actually install the `.scr` (see `docs/BUILD.md` → Installing, or the repo
  README) and open that Settings page, which will embed the live preview
  automatically once FlipClock is selected in the dropdown.

## Building from the command line

If you prefer not to open the IDE, MSBuild (installed with Visual Studio)
can build the same solution:

```powershell
# From a "Developer PowerShell for VS 2022" prompt:
msbuild FlipClockScreensaver.sln /p:Configuration=Release /p:Platform=x64 /m
```

## Troubleshooting

**"Cannot open include file: 'd2d1_1.h'" or similar SDK header errors**
The Windows 10 SDK component of the C++ workload isn't installed, or the
project's `WindowsTargetPlatformVersion` doesn't match an installed SDK.
Open Visual Studio Installer → Modify → confirm "Windows 10 SDK" is checked,
or right-click the project → **Retarget Solution** to pick whichever SDK
version you have installed.

**Linker errors for `D2D1CreateFactory`, `MFStartup`, etc.**
These come from `d2d1.lib` / `mfplat.lib` and friends, which are already
listed in the project's linker settings (`AdditionalDependencies` in
`FlipClockScreensaver.vcxproj`). If you've added a new source file that pulls
in an additional SDK component the existing list doesn't cover, add the
matching `.lib` there.

**Manifest / resource ID conflict at link time**
The project deliberately sets `<GenerateManifest>false</GenerateManifest>`
because `resources/FlipClock.rc` embeds a custom manifest at resource ID 1
(`resources/FlipClock.manifest`, for per-monitor-v2 DPI awareness and
Common Controls v6). If you re-enable `GenerateManifest`, you'll get a
duplicate resource ID 1 error — leave it off, or remove the `1 24 "..."`
line from the `.rc` file if you'd rather rely on the auto-generated one
(you'll lose the explicit Common-Controls-v6 dependency in that case, and
the settings dialog's controls will fall back to unthemed classic styling).

**The screensaver doesn't appear in Settings' dropdown after building**
Building only produces the `.scr` in `bin\x64\Release\`; Windows only
enumerates screensavers from `%WINDIR%\System32`. Run
`installer\Install-FlipClockScreensaver.ps1` (as Administrator) to copy it
there, or copy it there yourself.
