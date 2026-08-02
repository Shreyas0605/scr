#include "SettingsDialog.h"
#include "../config/Themes.h"
#include "../core/TimeZoneUtil.h"
#include "../../resources/resource.h"
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace fcs::settings {

namespace {

// ---------------------------------------------------------------------
// Control IDs. Grouped by tab; kept local to this file since the dialog
// is built programmatically (CreateWindowEx) rather than from an .rc
// DIALOGEX template, so no resource.h symbol needs to match a template.
// ---------------------------------------------------------------------
enum : int {
    IDC_TAB = 100,
    IDC_OK,
    IDC_CANCEL,
    IDC_APPLY,

    // General
    IDC_GEN_INFO,
    IDC_GEN_RESTORE_DEFAULTS,

    // Background
    IDC_BG_MODE_COMBO,
    IDC_BG_SOLID_SWATCH,
    IDC_BG_SOLID_PICK,
    IDC_BG_IMAGE_PATH,
    IDC_BG_IMAGE_BROWSE,
    IDC_BG_IMAGE_SCALE_COMBO,
    IDC_BG_SLIDESHOW_FOLDER,
    IDC_BG_SLIDESHOW_BROWSE,
    IDC_BG_SLIDESHOW_SHUFFLE,
    IDC_BG_SLIDESHOW_INTERVAL,
    IDC_BG_VIDEO_PATH,
    IDC_BG_VIDEO_BROWSE,
    IDC_BG_VIDEO_LOOP,
    IDC_BG_VIDEO_MUTED,
    IDC_BG_BLUR_SLIDER,
    IDC_BG_BLUR_LABEL,
    IDC_BG_BRIGHTNESS_SLIDER,
    IDC_BG_BRIGHTNESS_LABEL,

    // Clock
    IDC_CLOCK_12H,
    IDC_CLOCK_24H,
    IDC_CLOCK_SHOW_SECONDS,
    IDC_CLOCK_SHOW_DATE,
    IDC_CLOCK_SHOW_WEEKDAY,
    IDC_CLOCK_TIMEZONE_COMBO,
    IDC_CLOCK_FLIP_SPEED_SLIDER,
    IDC_CLOCK_FLIP_SPEED_LABEL,

    // Appearance
    IDC_APP_THEME_COMBO,
    IDC_APP_CORNER_SLIDER,
    IDC_APP_CORNER_LABEL,
    IDC_APP_FONT_COMBO,

    // Performance
    IDC_PERF_VSYNC,
    IDC_PERF_FPS_COMBO,
    IDC_PERF_SLEEP_HIDDEN,

    // About
    IDC_ABOUT_TEXT,

    IDC_FIRST_DYNAMIC
};

constexpr wchar_t kWndClassName[] = L"FlipClockSettingsWndClass";
constexpr int kTabCount = 6;
constexpr int kWindowWidth = 660;
constexpr int kWindowHeight = 560;

// One page's worth of child HWNDs, so we can show/hide by tab selection
// without tearing down and rebuilding controls every time.
struct DialogState {
    HWND hwnd = nullptr;
    HWND tab = nullptr;
    fcs::config::Settings* settings = nullptr;
    fcs::config::Settings working; // edited copy; only committed on OK/Apply
    std::array<std::vector<HWND>, kTabCount> tabControls;
    bool dirtyIgnore = false; // suppresses handlers while we're populating controls
};

HWND MakeChild(HWND parent, HINSTANCE hInst, const wchar_t* cls, const wchar_t* text, DWORD style,
               int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, cls, text, style | WS_CHILD, x, y, w, h, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
}

HWND MakeLabel(HWND parent, HINSTANCE hInst, const wchar_t* text, int x, int y, int w, int h) {
    return MakeChild(parent, hInst, L"STATIC", text, WS_VISIBLE | SS_LEFT, x, y, w, h, 0);
}

void SetFontRecursive(HWND parent, HFONT font) {
    SendMessageW(parent, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

// ---------------------------------------------------------------------
// Per-tab control construction. Each Build* function creates its
// controls (initially hidden; visibility toggled by tab selection),
// registers them in state.tabControls[tabIndex], and populates initial
// values from state.working.
// ---------------------------------------------------------------------

constexpr int kPageX = 24;
constexpr int kPageY = 60;
constexpr int kPageW = kWindowWidth - 2 * kPageX - 16;

void BuildGeneralTab(DialogState& state, HINSTANCE hInst) {
    auto& list = state.tabControls[0];
    HWND info = MakeChild(
        state.hwnd, hInst, L"STATIC",
        L"FlipClock Screensaver\r\n\r\n"
        L"A premium, GPU-accelerated split-flap clock screensaver.\r\n\r\n"
        L"Use the tabs above to configure the background, clock display, "
        L"visual theme, and performance behavior. Changes take effect the "
        L"next time the screensaver runs (use \"Preview\" from the Windows "
        L"screensaver picker to see them live).",
        WS_VISIBLE | SS_LEFT, kPageX, kPageY, kPageW, 160, IDC_GEN_INFO);
    HWND restore = MakeChild(state.hwnd, hInst, L"BUTTON", L"Restore Defaults",
                              WS_VISIBLE | BS_PUSHBUTTON, kPageX, kPageY + 180, 160, 28,
                              IDC_GEN_RESTORE_DEFAULTS);
    list = {info, restore};
}

void BuildBackgroundTab(DialogState& state, HINSTANCE hInst) {
    auto& list = state.tabControls[1];
    int y = kPageY;

    MakeLabel(state.hwnd, hInst, L"Background mode:", kPageX, y, 160, 20);
    HWND modeCombo = MakeChild(state.hwnd, hInst, L"COMBOBOX", nullptr,
                                WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, kPageX + 170, y - 2,
                                220, 200, IDC_BG_MODE_COMBO);
    for (const wchar_t* item : {L"Solid Color", L"Image", L"Slideshow", L"Video", L"Animated Gradient"})
        ComboBox_AddString(modeCombo, item);
    y += 32;

    HWND imgLabel = MakeLabel(state.hwnd, hInst, L"Image path:", kPageX, y, 160, 20);
    HWND imgPath = MakeChild(state.hwnd, hInst, L"EDIT", nullptr,
                              WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, kPageX + 170, y - 2, 300, 22,
                              IDC_BG_IMAGE_PATH);
    HWND imgBrowse = MakeChild(state.hwnd, hInst, L"BUTTON", L"Browse...", WS_VISIBLE | BS_PUSHBUTTON,
                                kPageX + 478, y - 3, 90, 24, IDC_BG_IMAGE_BROWSE);
    y += 28;
    HWND scaleLabel = MakeLabel(state.hwnd, hInst, L"Scale mode:", kPageX, y, 160, 20);
    HWND scaleCombo = MakeChild(state.hwnd, hInst, L"COMBOBOX", nullptr,
                                 WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, kPageX + 170, y - 2,
                                 220, 160, IDC_BG_IMAGE_SCALE_COMBO);
    for (const wchar_t* item : {L"Fill", L"Fit", L"Stretch", L"Center", L"Tile"})
        ComboBox_AddString(scaleCombo, item);
    y += 36;

    HWND slideLabel = MakeLabel(state.hwnd, hInst, L"Slideshow folder:", kPageX, y, 160, 20);
    HWND slideFolder = MakeChild(state.hwnd, hInst, L"EDIT", nullptr,
                                  WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, kPageX + 170, y - 2, 300,
                                  22, IDC_BG_SLIDESHOW_FOLDER);
    HWND slideBrowse = MakeChild(state.hwnd, hInst, L"BUTTON", L"Browse...", WS_VISIBLE | BS_PUSHBUTTON,
                                  kPageX + 478, y - 3, 90, 24, IDC_BG_SLIDESHOW_BROWSE);
    y += 28;
    HWND slideShuffle = MakeChild(state.hwnd, hInst, L"BUTTON", L"Shuffle order",
                                   WS_VISIBLE | BS_AUTOCHECKBOX, kPageX + 170, y, 140, 22,
                                   IDC_BG_SLIDESHOW_SHUFFLE);
    HWND intervalLabel = MakeLabel(state.hwnd, hInst, L"Interval (sec):", kPageX + 320, y, 100, 20);
    HWND intervalEdit = MakeChild(state.hwnd, hInst, L"EDIT", nullptr,
                                   WS_VISIBLE | WS_BORDER | ES_NUMBER, kPageX + 420, y - 2, 60, 22,
                                   IDC_BG_SLIDESHOW_INTERVAL);
    y += 36;

    HWND videoLabel = MakeLabel(state.hwnd, hInst, L"Video path:", kPageX, y, 160, 20);
    HWND videoPath = MakeChild(state.hwnd, hInst, L"EDIT", nullptr,
                                WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, kPageX + 170, y - 2, 300, 22,
                                IDC_BG_VIDEO_PATH);
    HWND videoBrowse = MakeChild(state.hwnd, hInst, L"BUTTON", L"Browse...", WS_VISIBLE | BS_PUSHBUTTON,
                                  kPageX + 478, y - 3, 90, 24, IDC_BG_VIDEO_BROWSE);
    y += 28;
    HWND videoLoop = MakeChild(state.hwnd, hInst, L"BUTTON", L"Loop playback",
                                WS_VISIBLE | BS_AUTOCHECKBOX, kPageX + 170, y, 140, 22,
                                IDC_BG_VIDEO_LOOP);
    HWND videoMuted = MakeChild(state.hwnd, hInst, L"BUTTON", L"Muted", WS_VISIBLE | BS_AUTOCHECKBOX,
                                 kPageX + 320, y, 100, 22, IDC_BG_VIDEO_MUTED);
    y += 40;

    HWND blurLabel = MakeLabel(state.hwnd, hInst, L"Blur:", kPageX, y, 60, 20);
    HWND blurSlider = MakeChild(state.hwnd, hInst, TRACKBAR_CLASSW, nullptr,
                                 WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS, kPageX + 170, y - 4, 300, 28,
                                 IDC_BG_BLUR_SLIDER);
    SendMessageW(blurSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    HWND blurValue = MakeLabel(state.hwnd, hInst, L"0", kPageX + 480, y, 60, 20);
    SetWindowLongPtrW(blurValue, GWLP_ID, IDC_BG_BLUR_LABEL);
    y += 34;

    HWND brightLabel = MakeLabel(state.hwnd, hInst, L"Brightness:", kPageX, y, 100, 20);
    HWND brightSlider = MakeChild(state.hwnd, hInst, TRACKBAR_CLASSW, nullptr,
                                   WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS, kPageX + 170, y - 4, 300, 28,
                                   IDC_BG_BRIGHTNESS_SLIDER);
    SendMessageW(brightSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    HWND brightValue = MakeLabel(state.hwnd, hInst, L"100", kPageX + 480, y, 60, 20);
    SetWindowLongPtrW(brightValue, GWLP_ID, IDC_BG_BRIGHTNESS_LABEL);

    list = {modeCombo,   imgLabel,      imgPath,       imgBrowse,    scaleLabel,   scaleCombo,
            slideLabel,  slideFolder,   slideBrowse,   slideShuffle, intervalLabel, intervalEdit,
            videoLabel,  videoPath,     videoBrowse,   videoLoop,    videoMuted,
            blurLabel,   blurSlider,    blurValue,     brightLabel,  brightSlider, brightValue};
}

void BuildClockTab(DialogState& state, HINSTANCE hInst) {
    auto& list = state.tabControls[2];
    int y = kPageY;

    HWND h12 = MakeChild(state.hwnd, hInst, L"BUTTON", L"12-hour", WS_VISIBLE | BS_AUTORADIOBUTTON,
                          kPageX, y, 100, 22, IDC_CLOCK_12H);
    HWND h24 = MakeChild(state.hwnd, hInst, L"BUTTON", L"24-hour", WS_VISIBLE | BS_AUTORADIOBUTTON,
                          kPageX + 110, y, 100, 22, IDC_CLOCK_24H);
    y += 34;
    HWND seconds = MakeChild(state.hwnd, hInst, L"BUTTON", L"Show seconds",
                              WS_VISIBLE | BS_AUTOCHECKBOX, kPageX, y, 160, 22,
                              IDC_CLOCK_SHOW_SECONDS);
    y += 30;
    HWND date = MakeChild(state.hwnd, hInst, L"BUTTON", L"Show date", WS_VISIBLE | BS_AUTOCHECKBOX,
                           kPageX, y, 160, 22, IDC_CLOCK_SHOW_DATE);
    y += 30;
    HWND weekday = MakeChild(state.hwnd, hInst, L"BUTTON", L"Show weekday",
                              WS_VISIBLE | BS_AUTOCHECKBOX, kPageX, y, 160, 22,
                              IDC_CLOCK_SHOW_WEEKDAY);
    y += 38;

    HWND tzLabel = MakeLabel(state.hwnd, hInst, L"Time zone:", kPageX, y, 100, 20);
    HWND tzCombo = MakeChild(state.hwnd, hInst, L"COMBOBOX", nullptr,
                              WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, kPageX + 110, y - 2, 400,
                              300, IDC_CLOCK_TIMEZONE_COMBO);
    ComboBox_AddString(tzCombo, L"(System Local Time)");
    for (const auto& name : fcs::core::TimeZoneUtil::EnumerateDisplayNames())
        ComboBox_AddString(tzCombo, name.c_str());
    y += 38;

    HWND speedLabel = MakeLabel(state.hwnd, hInst, L"Flip speed:", kPageX, y, 100, 20);
    HWND speedSlider = MakeChild(state.hwnd, hInst, TRACKBAR_CLASSW, nullptr,
                                  WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS, kPageX + 110, y - 4, 300, 28,
                                  IDC_CLOCK_FLIP_SPEED_SLIDER);
    SendMessageW(speedSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100)); // maps to 250-350ms
    HWND speedValue = MakeLabel(state.hwnd, hInst, L"300 ms", kPageX + 420, y, 80, 20);
    SetWindowLongPtrW(speedValue, GWLP_ID, IDC_CLOCK_FLIP_SPEED_LABEL);

    list = {h12, h24, seconds, date, weekday, tzLabel, tzCombo, speedLabel, speedSlider, speedValue};
}

void BuildAppearanceTab(DialogState& state, HINSTANCE hInst) {
    auto& list = state.tabControls[3];
    int y = kPageY;

    HWND themeLabel = MakeLabel(state.hwnd, hInst, L"Theme:", kPageX, y, 100, 20);
    HWND themeCombo = MakeChild(state.hwnd, hInst, L"COMBOBOX", nullptr,
                                 WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, kPageX + 110, y - 2, 260,
                                 220, IDC_APP_THEME_COMBO);
    for (const auto& theme : fcs::config::BuiltInThemes())
        ComboBox_AddString(themeCombo, theme.name.c_str());
    y += 38;

    HWND cornerLabel = MakeLabel(state.hwnd, hInst, L"Corner radius:", kPageX, y, 110, 20);
    HWND cornerSlider = MakeChild(state.hwnd, hInst, TRACKBAR_CLASSW, nullptr,
                                   WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS, kPageX + 120, y - 4, 260, 28,
                                   IDC_APP_CORNER_SLIDER);
    SendMessageW(cornerSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 30));
    HWND cornerValue = MakeLabel(state.hwnd, hInst, L"10 px", kPageX + 400, y, 80, 20);
    SetWindowLongPtrW(cornerValue, GWLP_ID, IDC_APP_CORNER_LABEL);
    y += 38;

    HWND fontLabel = MakeLabel(state.hwnd, hInst, L"Digit font:", kPageX, y, 100, 20);
    HWND fontCombo = MakeChild(state.hwnd, hInst, L"COMBOBOX", nullptr,
                                WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, kPageX + 110, y - 2, 260,
                                200, IDC_APP_FONT_COMBO);
    for (const wchar_t* f : {L"Segoe UI Semibold", L"Segoe UI", L"Consolas", L"Courier New",
                             L"Georgia", L"Arial"})
        ComboBox_AddString(fontCombo, f);

    list = {themeLabel, themeCombo, cornerLabel, cornerSlider, cornerValue, fontLabel, fontCombo};
}

void BuildPerformanceTab(DialogState& state, HINSTANCE hInst) {
    auto& list = state.tabControls[4];
    int y = kPageY;

    HWND vsync = MakeChild(state.hwnd, hInst, L"BUTTON", L"Enable VSync (recommended)",
                            WS_VISIBLE | BS_AUTOCHECKBOX, kPageX, y, 260, 22, IDC_PERF_VSYNC);
    y += 34;
    HWND fpsLabel = MakeLabel(state.hwnd, hInst, L"Target frame rate:", kPageX, y, 140, 20);
    HWND fpsCombo = MakeChild(state.hwnd, hInst, L"COMBOBOX", nullptr,
                               WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, kPageX + 150, y - 2, 120,
                               160, IDC_PERF_FPS_COMBO);
    for (const wchar_t* f : {L"30", L"60", L"75", L"120", L"144"}) ComboBox_AddString(fpsCombo, f);
    y += 34;
    HWND sleepHidden = MakeChild(state.hwnd, hInst, L"BUTTON", L"Pause rendering when hidden",
                                  WS_VISIBLE | BS_AUTOCHECKBOX, kPageX, y, 260, 22,
                                  IDC_PERF_SLEEP_HIDDEN);

    list = {vsync, fpsLabel, fpsCombo, sleepHidden};
}

void BuildAboutTab(DialogState& state, HINSTANCE hInst) {
    auto& list = state.tabControls[5];
    HWND about = MakeChild(
        state.hwnd, hInst, L"STATIC",
        L"FlipClock Screensaver\r\nVersion 1.0.0\r\n\r\n"
        L"A native Windows split-flap clock screensaver built with Direct2D, "
        L"DirectWrite, and Media Foundation.\r\n\r\n"
        L"\u00A9 2026. All rights reserved.",
        WS_VISIBLE | SS_LEFT, kPageX, kPageY, kPageW, 160, IDC_ABOUT_TEXT);
    list = {about};
}

// ---------------------------------------------------------------------
// Populate / harvest: copy state.working -> controls, and controls ->
// state.working, respectively.
// ---------------------------------------------------------------------

void PopulateControls(DialogState& state) {
    state.dirtyIgnore = true;
    const auto& s = state.working;

    // Background
    ComboBox_SetCurSel(GetDlgItem(state.hwnd, IDC_BG_MODE_COMBO),
                        static_cast<int>(s.background.mode));
    SetWindowTextW(GetDlgItem(state.hwnd, IDC_BG_IMAGE_PATH), s.background.imagePath.c_str());
    ComboBox_SetCurSel(GetDlgItem(state.hwnd, IDC_BG_IMAGE_SCALE_COMBO),
                        static_cast<int>(s.background.imageScaleMode));
    SetWindowTextW(GetDlgItem(state.hwnd, IDC_BG_SLIDESHOW_FOLDER), s.background.slideshowFolder.c_str());
    Button_SetCheck(GetDlgItem(state.hwnd, IDC_BG_SLIDESHOW_SHUFFLE),
                     s.background.slideshowShuffle ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(GetDlgItem(state.hwnd, IDC_BG_SLIDESHOW_INTERVAL),
                   std::to_wstring(static_cast<int>(s.background.slideshowIntervalSeconds)).c_str());
    SetWindowTextW(GetDlgItem(state.hwnd, IDC_BG_VIDEO_PATH), s.background.videoPath.c_str());
    Button_SetCheck(GetDlgItem(state.hwnd, IDC_BG_VIDEO_LOOP),
                     s.background.videoLoop ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(state.hwnd, IDC_BG_VIDEO_MUTED),
                     s.background.videoMuted ? BST_CHECKED : BST_UNCHECKED);
    SendMessageW(GetDlgItem(state.hwnd, IDC_BG_BLUR_SLIDER), TBM_SETPOS, TRUE,
                 s.background.blurAmount);
    SetWindowTextW(GetDlgItem(state.hwnd, IDC_BG_BLUR_LABEL),
                   std::to_wstring(s.background.blurAmount).c_str());
    SendMessageW(GetDlgItem(state.hwnd, IDC_BG_BRIGHTNESS_SLIDER), TBM_SETPOS, TRUE,
                 s.background.brightnessAmount);
    SetWindowTextW(GetDlgItem(state.hwnd, IDC_BG_BRIGHTNESS_LABEL),
                   std::to_wstring(s.background.brightnessAmount).c_str());

    // Clock
    Button_SetCheck(GetDlgItem(state.hwnd, IDC_CLOCK_12H),
                     s.clock.hourFormat == fcs::config::HourFormatSetting::H12 ? BST_CHECKED
                                                                                : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(state.hwnd, IDC_CLOCK_24H),
                     s.clock.hourFormat == fcs::config::HourFormatSetting::H24 ? BST_CHECKED
                                                                                : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(state.hwnd, IDC_CLOCK_SHOW_SECONDS),
                     s.clock.showSeconds ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(state.hwnd, IDC_CLOCK_SHOW_DATE),
                     s.clock.showDate ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(state.hwnd, IDC_CLOCK_SHOW_WEEKDAY),
                     s.clock.showWeekday ? BST_CHECKED : BST_UNCHECKED);
    {
        HWND tzCombo = GetDlgItem(state.hwnd, IDC_CLOCK_TIMEZONE_COMBO);
        int idx = s.clock.timezoneKey.empty()
                      ? 0
                      : ComboBox_FindStringExact(tzCombo, -1, s.clock.timezoneKey.c_str());
        ComboBox_SetCurSel(tzCombo, idx < 0 ? 0 : idx);
    }
    {
        const int ms = static_cast<int>(s.clock.flipDurationSeconds * 1000.0);
        const int pos = std::clamp(ms - 250, 0, 100);
        SendMessageW(GetDlgItem(state.hwnd, IDC_CLOCK_FLIP_SPEED_SLIDER), TBM_SETPOS, TRUE, pos);
        SetWindowTextW(GetDlgItem(state.hwnd, IDC_CLOCK_FLIP_SPEED_LABEL),
                       (std::to_wstring(ms) + L" ms").c_str());
    }

    // Appearance
    {
        HWND themeCombo = GetDlgItem(state.hwnd, IDC_APP_THEME_COMBO);
        int idx = ComboBox_FindStringExact(themeCombo, -1, s.theme.name.c_str());
        ComboBox_SetCurSel(themeCombo, idx < 0 ? 0 : idx);
    }
    SendMessageW(GetDlgItem(state.hwnd, IDC_APP_CORNER_SLIDER), TBM_SETPOS, TRUE,
                 static_cast<int>(s.theme.cornerRadius));
    SetWindowTextW(GetDlgItem(state.hwnd, IDC_APP_CORNER_LABEL),
                   (std::to_wstring(static_cast<int>(s.theme.cornerRadius)) + L" px").c_str());
    {
        HWND fontCombo = GetDlgItem(state.hwnd, IDC_APP_FONT_COMBO);
        int idx = ComboBox_FindStringExact(fontCombo, -1, s.theme.fontFamily.c_str());
        ComboBox_SetCurSel(fontCombo, idx < 0 ? 0 : idx);
    }

    // Performance
    Button_SetCheck(GetDlgItem(state.hwnd, IDC_PERF_VSYNC),
                     s.performance.vsync ? BST_CHECKED : BST_UNCHECKED);
    {
        HWND fpsCombo = GetDlgItem(state.hwnd, IDC_PERF_FPS_COMBO);
        int idx = ComboBox_FindStringExact(fpsCombo, -1,
                                            std::to_wstring(s.performance.targetFps).c_str());
        ComboBox_SetCurSel(fpsCombo, idx < 0 ? 1 : idx);
    }
    Button_SetCheck(GetDlgItem(state.hwnd, IDC_PERF_SLEEP_HIDDEN),
                     s.performance.sleepWhenHidden ? BST_CHECKED : BST_UNCHECKED);

    state.dirtyIgnore = false;
}

std::wstring GetText(HWND h) {
    wchar_t buf[1024];
    GetWindowTextW(h, buf, 1024);
    return buf;
}

void HarvestControls(DialogState& state) {
    auto& s = state.working;

    s.background.mode = static_cast<fcs::config::BackgroundMode>(
        ComboBox_GetCurSel(GetDlgItem(state.hwnd, IDC_BG_MODE_COMBO)));
    s.background.imagePath = GetText(GetDlgItem(state.hwnd, IDC_BG_IMAGE_PATH));
    s.background.imageScaleMode = static_cast<fcs::config::ScaleMode>(
        ComboBox_GetCurSel(GetDlgItem(state.hwnd, IDC_BG_IMAGE_SCALE_COMBO)));
    s.background.slideshowFolder = GetText(GetDlgItem(state.hwnd, IDC_BG_SLIDESHOW_FOLDER));
    s.background.slideshowShuffle =
        Button_GetCheck(GetDlgItem(state.hwnd, IDC_BG_SLIDESHOW_SHUFFLE)) == BST_CHECKED;
    s.background.slideshowIntervalSeconds =
        _wtof(GetText(GetDlgItem(state.hwnd, IDC_BG_SLIDESHOW_INTERVAL)).c_str());
    if (s.background.slideshowIntervalSeconds <= 0.0) s.background.slideshowIntervalSeconds = 30.0;
    s.background.videoPath = GetText(GetDlgItem(state.hwnd, IDC_BG_VIDEO_PATH));
    s.background.videoLoop = Button_GetCheck(GetDlgItem(state.hwnd, IDC_BG_VIDEO_LOOP)) == BST_CHECKED;
    s.background.videoMuted =
        Button_GetCheck(GetDlgItem(state.hwnd, IDC_BG_VIDEO_MUTED)) == BST_CHECKED;
    s.background.blurAmount = static_cast<int>(
        SendMessageW(GetDlgItem(state.hwnd, IDC_BG_BLUR_SLIDER), TBM_GETPOS, 0, 0));
    s.background.brightnessAmount = static_cast<int>(
        SendMessageW(GetDlgItem(state.hwnd, IDC_BG_BRIGHTNESS_SLIDER), TBM_GETPOS, 0, 0));

    s.clock.hourFormat = Button_GetCheck(GetDlgItem(state.hwnd, IDC_CLOCK_12H)) == BST_CHECKED
                              ? fcs::config::HourFormatSetting::H12
                              : fcs::config::HourFormatSetting::H24;
    s.clock.showSeconds = Button_GetCheck(GetDlgItem(state.hwnd, IDC_CLOCK_SHOW_SECONDS)) == BST_CHECKED;
    s.clock.showDate = Button_GetCheck(GetDlgItem(state.hwnd, IDC_CLOCK_SHOW_DATE)) == BST_CHECKED;
    s.clock.showWeekday = Button_GetCheck(GetDlgItem(state.hwnd, IDC_CLOCK_SHOW_WEEKDAY)) == BST_CHECKED;
    {
        HWND tzCombo = GetDlgItem(state.hwnd, IDC_CLOCK_TIMEZONE_COMBO);
        int idx = ComboBox_GetCurSel(tzCombo);
        s.clock.timezoneKey = idx <= 0 ? L"" : GetText(tzCombo);
    }
    {
        const int pos = static_cast<int>(
            SendMessageW(GetDlgItem(state.hwnd, IDC_CLOCK_FLIP_SPEED_SLIDER), TBM_GETPOS, 0, 0));
        s.clock.flipDurationSeconds = (250 + pos) / 1000.0;
    }

    {
        HWND themeCombo = GetDlgItem(state.hwnd, IDC_APP_THEME_COMBO);
        int idx = ComboBox_GetCurSel(themeCombo);
        if (idx >= 0) {
            const auto& themes = fcs::config::BuiltInThemes();
            if (static_cast<size_t>(idx) < themes.size()) {
                fcs::config::ThemeSetting picked = themes[idx];
                // Corner radius and font are independently adjustable, so
                // don't blindly overwrite them if the user tweaked the
                // slider/combo away from the preset's defaults.
                picked.cornerRadius = static_cast<float>(
                    SendMessageW(GetDlgItem(state.hwnd, IDC_APP_CORNER_SLIDER), TBM_GETPOS, 0, 0));
                picked.fontFamily = GetText(GetDlgItem(state.hwnd, IDC_APP_FONT_COMBO));
                s.theme = picked;
            }
        }
    }

    s.performance.vsync = Button_GetCheck(GetDlgItem(state.hwnd, IDC_PERF_VSYNC)) == BST_CHECKED;
    s.performance.targetFps = _wtoi(GetText(GetDlgItem(state.hwnd, IDC_PERF_FPS_COMBO)).c_str());
    if (s.performance.targetFps <= 0) s.performance.targetFps = 60;
    s.performance.sleepWhenHidden =
        Button_GetCheck(GetDlgItem(state.hwnd, IDC_PERF_SLEEP_HIDDEN)) == BST_CHECKED;
}

void ShowTab(DialogState& state, int index) {
    for (int t = 0; t < kTabCount; ++t) {
        const int show = (t == index) ? SW_SHOW : SW_HIDE;
        for (HWND h : state.tabControls[t]) ShowWindow(h, show);
    }
}

void UpdateSliderLabel(HWND slider, HWND label, const std::wstring& suffix, int offset = 0) {
    const int pos = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0)) + offset;
    SetWindowTextW(label, (std::to_wstring(pos) + suffix).c_str());
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DialogState* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lParam);
            if (state && nm->hwndFrom == state->tab && nm->code == TCN_SELCHANGE) {
                ShowTab(*state, TabCtrl_GetCurSel(state->tab));
            }
            return 0;
        }

        case WM_HSCROLL: {
            if (!state || state->dirtyIgnore) return 0;
            HWND ctl = reinterpret_cast<HWND>(lParam);
            if (ctl == GetDlgItem(hwnd, IDC_BG_BLUR_SLIDER)) {
                UpdateSliderLabel(ctl, GetDlgItem(hwnd, IDC_BG_BLUR_LABEL), L"");
            } else if (ctl == GetDlgItem(hwnd, IDC_BG_BRIGHTNESS_SLIDER)) {
                UpdateSliderLabel(ctl, GetDlgItem(hwnd, IDC_BG_BRIGHTNESS_LABEL), L"");
            } else if (ctl == GetDlgItem(hwnd, IDC_CLOCK_FLIP_SPEED_SLIDER)) {
                UpdateSliderLabel(ctl, GetDlgItem(hwnd, IDC_CLOCK_FLIP_SPEED_LABEL), L" ms", 250);
            } else if (ctl == GetDlgItem(hwnd, IDC_APP_CORNER_SLIDER)) {
                UpdateSliderLabel(ctl, GetDlgItem(hwnd, IDC_APP_CORNER_LABEL), L" px");
            }
            return 0;
        }

        case WM_COMMAND: {
            if (!state) break;
            const int id = LOWORD(wParam);

            if (id == IDC_BG_IMAGE_BROWSE || id == IDC_BG_VIDEO_BROWSE) {
                wchar_t file[MAX_PATH] = L"";
                OPENFILENAMEW ofn{};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFile = file;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrFilter = (id == IDC_BG_IMAGE_BROWSE)
                                      ? L"Images\0*.png;*.jpg;*.jpeg;*.webp;*.bmp;*.heic\0All Files\0*.*\0"
                                      : L"Videos\0*.mp4;*.mov;*.avi;*.webm\0All Files\0*.*\0";
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    SetWindowTextW(GetDlgItem(hwnd, id == IDC_BG_IMAGE_BROWSE ? IDC_BG_IMAGE_PATH
                                                                               : IDC_BG_VIDEO_PATH),
                                   file);
                }
                return 0;
            }
            if (id == IDC_BG_SLIDESHOW_BROWSE) {
                BROWSEINFOW bi{};
                bi.hwndOwner = hwnd;
                bi.lpszTitle = L"Choose a folder of images for the slideshow";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
                if (pidl) {
                    wchar_t path[MAX_PATH];
                    if (SHGetPathFromIDListW(pidl, path))
                        SetWindowTextW(GetDlgItem(hwnd, IDC_BG_SLIDESHOW_FOLDER), path);
                    CoTaskMemFree(pidl);
                }
                return 0;
            }
            if (id == IDC_GEN_RESTORE_DEFAULTS) {
                if (MessageBoxW(hwnd, L"Reset all settings to their defaults?", L"FlipClock Screensaver",
                                 MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    state->working = fcs::config::Settings::Default();
                    PopulateControls(*state);
                }
                return 0;
            }
            if (id == IDC_OK || id == IDC_APPLY) {
                HarvestControls(*state);
                fcs::config::SaveSettings(state->working);
                *state->settings = state->working;
                if (id == IDC_OK) DestroyWindow(hwnd);
                return 0;
            }
            if (id == IDC_CANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RegisterClassOnce(HINSTANCE hInst) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWndClassName;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    RegisterClassExW(&wc);
    registered = true;
}

} // namespace

void ShowSettingsDialog(HINSTANCE hInstance, HWND parentHwnd, fcs::config::Settings& settings) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    RegisterClassOnce(hInstance);

    DialogState state;
    state.settings = &settings;
    state.working = settings;

    RECT r{0, 0, kWindowWidth, kWindowHeight};
    AdjustWindowRectEx(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);

    state.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kWndClassName, L"FlipClock Screensaver Settings",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                                  parentHwnd, nullptr, hInstance, nullptr);
    if (!state.hwnd) return;

    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    if (parentHwnd && IsWindow(parentHwnd)) EnableWindow(parentHwnd, FALSE);

    HFONT uiFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    state.tab = CreateWindowExW(0, WC_TABCONTROLW, nullptr, WS_CHILD | WS_VISIBLE, 10, 10,
                                 kWindowWidth - 36, kWindowHeight - 90, state.hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TAB)), hInstance,
                                 nullptr);
    const wchar_t* tabNames[kTabCount] = {L"General", L"Background", L"Clock",
                                           L"Appearance", L"Performance", L"About"};
    for (int i = 0; i < kTabCount; ++i) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(tabNames[i]);
        TabCtrl_InsertItem(state.tab, i, &item);
    }

    BuildGeneralTab(state, hInstance);
    BuildBackgroundTab(state, hInstance);
    BuildClockTab(state, hInstance);
    BuildAppearanceTab(state, hInstance);
    BuildPerformanceTab(state, hInstance);
    BuildAboutTab(state, hInstance);

    const int btnY = kWindowHeight - 48;
    HWND okBtn = MakeChild(state.hwnd, hInstance, L"BUTTON", L"OK",
                            WS_VISIBLE | BS_DEFPUSHBUTTON, kWindowWidth - 280, btnY, 80, 28, IDC_OK);
    HWND cancelBtn = MakeChild(state.hwnd, hInstance, L"BUTTON", L"Cancel", WS_VISIBLE | BS_PUSHBUTTON,
                                kWindowWidth - 190, btnY, 80, 28, IDC_CANCEL);
    HWND applyBtn = MakeChild(state.hwnd, hInstance, L"BUTTON", L"Apply", WS_VISIBLE | BS_PUSHBUTTON,
                               kWindowWidth - 100, btnY, 80, 28, IDC_APPLY);

    // Apply the UI font to every control we just created (tab + all pages
    // + the three action buttons) for a consistent, modern look instead of
    // the default raster "System" font.
    SetFontRecursive(state.tab, uiFont);
    for (auto& page : state.tabControls)
        for (HWND h : page) SetFontRecursive(h, uiFont);
    SetFontRecursive(okBtn, uiFont);
    SetFontRecursive(cancelBtn, uiFont);
    SetFontRecursive(applyBtn, uiFont);

    PopulateControls(state);
    TabCtrl_SetCurSel(state.tab, 0);
    ShowTab(state, 0);

    if (HMONITOR mon = MonitorFromWindow(state.hwnd, MONITOR_DEFAULTTONEAREST)) {
        MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfoW(mon, &mi)) {
            RECT wr;
            GetWindowRect(state.hwnd, &wr);
            const int w = wr.right - wr.left;
            const int h = wr.bottom - wr.top;
            const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
            const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
            SetWindowPos(state.hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
    }

    ShowWindow(state.hwnd, SW_SHOW);
    UpdateWindow(state.hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(state.hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(state.hwnd)) break;
    }

    if (parentHwnd && IsWindow(parentHwnd)) {
        EnableWindow(parentHwnd, TRUE);
        SetForegroundWindow(parentHwnd);
    }
    DeleteObject(uiFont);
}

} // namespace fcs::settings
