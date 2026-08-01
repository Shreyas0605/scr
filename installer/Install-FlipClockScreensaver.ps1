#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Installs FlipClock.scr so it appears in Windows Settings >
    Personalization > Lock screen > Screen saver settings.

.DESCRIPTION
    Windows enumerates available screensavers by scanning %WINDIR%\System32
    (and, on 64-bit Windows, %WINDIR%\SysWOW64 for 32-bit builds) for *.scr
    files - there is no registry entry to create and no COM registration
    needed. Explorer's right-click "Install", "Test", and (once installed
    and selected as active) the Settings page's "Screen saver settings"
    button all just re-invoke the .scr with /s, /p <hwnd>, and /c
    respectively, which FlipClock.scr already implements natively.

    This script therefore only needs to:
      1. Locate the freshly-built FlipClock.scr (Release|x64 by default).
      2. Copy it into %WINDIR%\System32\FlipClock.scr (requires admin).
      3. Optionally set it as the active screensaver via the registry keys
         Windows itself uses (HKCU\Control Panel\Desktop), so a fresh
         install can be verified without opening Settings by hand.

.PARAMETER ScrPath
    Path to the built FlipClock.scr. Defaults to the standard
    bin\x64\Release output location relative to the repo root.

.PARAMETER SetActive
    If specified, also sets FlipClock as the currently active screensaver
    and enables screensaver timeout (matching what selecting it in Settings
    would do).

.EXAMPLE
    .\Install-FlipClockScreensaver.ps1
    .\Install-FlipClockScreensaver.ps1 -SetActive
    .\Install-FlipClockScreensaver.ps1 -ScrPath "C:\build\FlipClock.scr" -SetActive
#>
param(
    [string]$ScrPath = "$PSScriptRoot\..\bin\x64\Release\FlipClock.scr",
    [switch]$SetActive
)

$ErrorActionPreference = "Stop"

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "    $msg" -ForegroundColor Green }
function Write-Err($msg)  { Write-Host "ERROR: $msg" -ForegroundColor Red }

$resolvedScr = Resolve-Path -Path $ScrPath -ErrorAction SilentlyContinue
if (-not $resolvedScr) {
    Write-Err "Could not find a built .scr at: $ScrPath"
    Write-Err "Build the Release|x64 configuration in Visual Studio first, or pass -ScrPath explicitly."
    exit 1
}
$ScrPath = $resolvedScr.Path

Write-Step "Found screensaver binary: $ScrPath"

$systemDir = Join-Path $env:WINDIR "System32"
$destination = Join-Path $systemDir "FlipClock.scr"

Write-Step "Copying to $destination (requires Administrator)"
Copy-Item -Path $ScrPath -Destination $destination -Force
Write-Ok "Installed."

Write-Step "Verifying it now appears in the Windows screensaver list"
$candidates = Get-ChildItem -Path $systemDir -Filter "*.scr" | Select-Object -ExpandProperty Name
if ($candidates -contains "FlipClock.scr") {
    Write-Ok "FlipClock.scr is present in $systemDir alongside: $($candidates -join ', ')"
} else {
    Write-Err "Copy appeared to succeed but FlipClock.scr was not found afterward. Check permissions."
    exit 1
}

if ($SetActive) {
    Write-Step "Setting FlipClock as the active screensaver (HKCU\Control Panel\Desktop)"
    Set-ItemProperty -Path "HKCU:\Control Panel\Desktop" -Name "SCRNSAVE.EXE" -Value $destination
    Set-ItemProperty -Path "HKCU:\Control Panel\Desktop" -Name "ScreenSaveActive" -Value "1"
    Set-ItemProperty -Path "HKCU:\Control Panel\Desktop" -Name "ScreenSaveTimeOut" -Value "600"
    Write-Ok "Active screensaver set (10 minute default timeout)."
}

Write-Host ""
Write-Ok "Done. Open Settings > Personalization > Lock screen > Screen saver settings to configure and preview it,"
Write-Ok "or right-click FlipClock.scr in Explorer for the Install / Test / Open (Configure) verbs directly."
