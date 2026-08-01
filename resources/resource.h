#pragma once

// Icon, referenced by FlipClock.rc and by SettingsDialog.cpp's window-class
// registration (LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON))).
#define IDI_APPICON 101

// Manifest is embedded via RT_MANIFEST resource id 1
// (CREATEPROCESS_MANIFEST_RESOURCE_ID) directly in FlipClock.rc; see
// resources/FlipClock.manifest for its contents.
