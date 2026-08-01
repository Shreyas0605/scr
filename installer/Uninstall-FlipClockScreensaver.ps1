#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Removes FlipClock.scr from Windows and clears it as the active
    screensaver if it's currently selected.
#>
$ErrorActionPreference = "Stop"

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "    $msg" -ForegroundColor Green }

$destination = Join-Path (Join-Path $env:WINDIR "System32") "FlipClock.scr"

$current = (Get-ItemProperty -Path "HKCU:\Control Panel\Desktop" -Name "SCRNSAVE.EXE" -ErrorAction SilentlyContinue).'SCRNSAVE.EXE'
if ($current -and ($current -ieq $destination)) {
    Write-Step "FlipClock is the active screensaver; clearing it"
    Set-ItemProperty -Path "HKCU:\Control Panel\Desktop" -Name "ScreenSaveActive" -Value "0"
    Remove-ItemProperty -Path "HKCU:\Control Panel\Desktop" -Name "SCRNSAVE.EXE" -ErrorAction SilentlyContinue
    Write-Ok "Cleared."
}

if (Test-Path $destination) {
    Write-Step "Removing $destination"
    Remove-Item -Path $destination -Force
    Write-Ok "Removed."
} else {
    Write-Ok "FlipClock.scr was not installed in System32; nothing to remove."
}
