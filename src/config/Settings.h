#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace fcs::config {

enum class BackgroundMode { SolidColor, Image, Slideshow, Video, AnimatedGradient };
enum class ScaleMode { Fill, Fit, Stretch, Center, Tile };
enum class HourFormatSetting { H12, H24 };

struct ColorSetting {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

struct ThemeSetting {
    std::wstring name = L"Minimal Black";
    ColorSetting tileFace{0.086f, 0.086f, 0.090f, 1.0f};
    ColorSetting tileFaceAlt{0.071f, 0.071f, 0.075f, 1.0f};
    ColorSetting digitColor{1.0f, 1.0f, 1.0f, 1.0f};
    ColorSetting labelColor{0.55f, 0.55f, 0.58f, 1.0f};
    ColorSetting backgroundColor{0.0f, 0.0f, 0.0f, 1.0f};
    float cornerRadius = 10.0f;
    std::wstring fontFamily = L"Segoe UI Semibold";
};

struct BackgroundSettings {
    BackgroundMode mode = BackgroundMode::SolidColor;
    ColorSetting solidColor{0.0f, 0.0f, 0.0f, 1.0f};

    std::wstring imagePath;
    ScaleMode imageScaleMode = ScaleMode::Fill;

    std::wstring slideshowFolder;
    bool slideshowShuffle = true;
    double slideshowIntervalSeconds = 30.0;
    double slideshowCrossfadeSeconds = 1.5;

    std::wstring videoPath;
    bool videoLoop = true;
    bool videoMuted = true;

    // Animated gradient: list of stop colors, animated by slowly rotating
    // the gradient axis / cycling stop positions.
    std::vector<ColorSetting> gradientStops{ColorSetting{0.02f, 0.02f, 0.05f, 1.0f},
                                             ColorSetting{0.05f, 0.02f, 0.08f, 1.0f}};
    double gradientCycleSeconds = 20.0;

    int blurAmount = 0;       // 0-100
    int brightnessAmount = 100; // 0-100, 100 = unmodified
};

struct ClockSettings {
    HourFormatSetting hourFormat = HourFormatSetting::H24;
    bool showSeconds = true;
    bool showDate = true;
    bool showWeekday = true;
    bool localeAware = true;
    std::wstring timezoneKey; // empty = system local
    double flipDurationSeconds = 0.30;
};

struct MonitorSettings {
    enum class MultiMonitorMode { Mirror, Independent, Span };
    MultiMonitorMode mode = MultiMonitorMode::Mirror;
};

struct PerformanceSettings {
    bool vsync = true;
    int targetFps = 60;
    bool sleepWhenHidden = true;
};

// Root configuration object. Serialized to/from
// %APPDATA%\FlipClockScreensaver\settings.json
struct Settings {
    ThemeSetting theme;
    BackgroundSettings background;
    ClockSettings clock;
    MonitorSettings monitor;
    PerformanceSettings performance;
    int schemaVersion = 1;

    static Settings Default();
};

// Loads settings from disk, applying Settings::Default() for any missing
// or malformed fields (never throws; invalid JSON yields full defaults).
Settings LoadSettings();

// Persists settings to disk, creating the config directory if needed.
// Returns false if writing failed (e.g. permissions), leaving the file
// untouched on disk.
bool SaveSettings(const Settings& settings);

// Full path to the settings JSON file used by Load/SaveSettings.
std::wstring SettingsFilePath();

} // namespace fcs::config
