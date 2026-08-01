#include "Settings.h"
#include "json.hpp"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "Shell32.lib")

using json = nlohmann::json;

namespace fcs::config {

namespace {

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(len > 0 ? len - 1 : 0, '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(len > 0 ? len - 1 : 0, L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

json ColorToJson(const ColorSetting& c) { return json{{"r", c.r}, {"g", c.g}, {"b", c.b}, {"a", c.a}}; }

ColorSetting ColorFromJson(const json& j, const ColorSetting& def) {
    ColorSetting c = def;
    if (!j.is_object()) return c;
    c.r = j.value("r", def.r);
    c.g = j.value("g", def.g);
    c.b = j.value("b", def.b);
    c.a = j.value("a", def.a);
    return c;
}

const char* BgModeToStr(BackgroundMode m) {
    switch (m) {
        case BackgroundMode::SolidColor: return "solid_color";
        case BackgroundMode::Image: return "image";
        case BackgroundMode::Slideshow: return "slideshow";
        case BackgroundMode::Video: return "video";
        case BackgroundMode::AnimatedGradient: return "animated_gradient";
    }
    return "solid_color";
}

BackgroundMode BgModeFromStr(const std::string& s) {
    if (s == "image") return BackgroundMode::Image;
    if (s == "slideshow") return BackgroundMode::Slideshow;
    if (s == "video") return BackgroundMode::Video;
    if (s == "animated_gradient") return BackgroundMode::AnimatedGradient;
    return BackgroundMode::SolidColor;
}

const char* ScaleModeToStr(ScaleMode m) {
    switch (m) {
        case ScaleMode::Fill: return "fill";
        case ScaleMode::Fit: return "fit";
        case ScaleMode::Stretch: return "stretch";
        case ScaleMode::Center: return "center";
        case ScaleMode::Tile: return "tile";
    }
    return "fill";
}

ScaleMode ScaleModeFromStr(const std::string& s) {
    if (s == "fit") return ScaleMode::Fit;
    if (s == "stretch") return ScaleMode::Stretch;
    if (s == "center") return ScaleMode::Center;
    if (s == "tile") return ScaleMode::Tile;
    return ScaleMode::Fill;
}

const char* MonitorModeToStr(MonitorSettings::MultiMonitorMode m) {
    switch (m) {
        case MonitorSettings::MultiMonitorMode::Independent: return "independent";
        case MonitorSettings::MultiMonitorMode::Span: return "span";
        default: return "mirror";
    }
}

MonitorSettings::MultiMonitorMode MonitorModeFromStr(const std::string& s) {
    if (s == "independent") return MonitorSettings::MultiMonitorMode::Independent;
    if (s == "span") return MonitorSettings::MultiMonitorMode::Span;
    return MonitorSettings::MultiMonitorMode::Mirror;
}

} // namespace

Settings Settings::Default() { return Settings{}; }

std::wstring SettingsFilePath() {
    PWSTR appDataPath = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath))) {
        result = appDataPath;
        CoTaskMemFree(appDataPath);
        result += L"\\FlipClockScreensaver";
        CreateDirectoryW(result.c_str(), nullptr);
        result += L"\\settings.json";
    }
    return result;
}

Settings LoadSettings() {
    Settings s = Settings::Default();
    const std::wstring path = SettingsFilePath();
    if (path.empty()) return s;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return s;

    std::stringstream buf;
    buf << file.rdbuf();
    const std::string content = buf.str();
    if (content.empty()) return s;

    json j;
    try {
        j = json::parse(content);
    } catch (...) {
        return s; // corrupt/invalid JSON -> defaults, never crash
    }

    try {
        if (j.contains("theme") && j["theme"].is_object()) {
            const auto& t = j["theme"];
            s.theme.name = FromUtf8(t.value("name", ToUtf8(s.theme.name)));
            s.theme.tileFace = ColorFromJson(t.value("tileFace", json{}), s.theme.tileFace);
            s.theme.tileFaceAlt = ColorFromJson(t.value("tileFaceAlt", json{}), s.theme.tileFaceAlt);
            s.theme.digitColor = ColorFromJson(t.value("digitColor", json{}), s.theme.digitColor);
            s.theme.labelColor = ColorFromJson(t.value("labelColor", json{}), s.theme.labelColor);
            s.theme.backgroundColor =
                ColorFromJson(t.value("backgroundColor", json{}), s.theme.backgroundColor);
            s.theme.cornerRadius = t.value("cornerRadius", s.theme.cornerRadius);
            s.theme.fontFamily = FromUtf8(t.value("fontFamily", ToUtf8(s.theme.fontFamily)));
        }

        if (j.contains("background") && j["background"].is_object()) {
            const auto& b = j["background"];
            s.background.mode = BgModeFromStr(b.value("mode", std::string(BgModeToStr(s.background.mode))));
            s.background.solidColor = ColorFromJson(b.value("solidColor", json{}), s.background.solidColor);
            s.background.imagePath = FromUtf8(b.value("imagePath", ""));
            s.background.imageScaleMode = ScaleModeFromStr(
                b.value("imageScaleMode", std::string(ScaleModeToStr(s.background.imageScaleMode))));
            s.background.slideshowFolder = FromUtf8(b.value("slideshowFolder", ""));
            s.background.slideshowShuffle = b.value("slideshowShuffle", s.background.slideshowShuffle);
            s.background.slideshowIntervalSeconds =
                b.value("slideshowIntervalSeconds", s.background.slideshowIntervalSeconds);
            s.background.slideshowCrossfadeSeconds =
                b.value("slideshowCrossfadeSeconds", s.background.slideshowCrossfadeSeconds);
            s.background.videoPath = FromUtf8(b.value("videoPath", ""));
            s.background.videoLoop = b.value("videoLoop", s.background.videoLoop);
            s.background.videoMuted = b.value("videoMuted", s.background.videoMuted);
            s.background.gradientCycleSeconds =
                b.value("gradientCycleSeconds", s.background.gradientCycleSeconds);
            s.background.blurAmount = b.value("blurAmount", s.background.blurAmount);
            s.background.brightnessAmount = b.value("brightnessAmount", s.background.brightnessAmount);
            if (b.contains("gradientStops") && b["gradientStops"].is_array()) {
                s.background.gradientStops.clear();
                for (const auto& stopJ : b["gradientStops"]) {
                    s.background.gradientStops.push_back(ColorFromJson(stopJ, ColorSetting{}));
                }
                if (s.background.gradientStops.empty()) s.background.gradientStops = Settings::Default().background.gradientStops;
            }
        }

        if (j.contains("clock") && j["clock"].is_object()) {
            const auto& c = j["clock"];
            s.clock.hourFormat =
                c.value("hourFormat", std::string("h24")) == "h12" ? HourFormatSetting::H12 : HourFormatSetting::H24;
            s.clock.showSeconds = c.value("showSeconds", s.clock.showSeconds);
            s.clock.showDate = c.value("showDate", s.clock.showDate);
            s.clock.showWeekday = c.value("showWeekday", s.clock.showWeekday);
            s.clock.localeAware = c.value("localeAware", s.clock.localeAware);
            s.clock.timezoneKey = FromUtf8(c.value("timezoneKey", ""));
            s.clock.flipDurationSeconds = c.value("flipDurationSeconds", s.clock.flipDurationSeconds);
        }

        if (j.contains("monitor") && j["monitor"].is_object()) {
            s.monitor.mode = MonitorModeFromStr(
                j["monitor"].value("mode", std::string(MonitorModeToStr(s.monitor.mode))));
        }

        if (j.contains("performance") && j["performance"].is_object()) {
            const auto& p = j["performance"];
            s.performance.vsync = p.value("vsync", s.performance.vsync);
            s.performance.targetFps = p.value("targetFps", s.performance.targetFps);
            s.performance.sleepWhenHidden = p.value("sleepWhenHidden", s.performance.sleepWhenHidden);
        }

        s.schemaVersion = j.value("schemaVersion", s.schemaVersion);
    } catch (...) {
        // Any unexpected shape mid-parse: keep whatever fields succeeded,
        // rest stay at their default-constructed values.
    }

    return s;
}

bool SaveSettings(const Settings& s) {
    const std::wstring path = SettingsFilePath();
    if (path.empty()) return false;

    json j;
    j["schemaVersion"] = s.schemaVersion;

    j["theme"] = {{"name", ToUtf8(s.theme.name)},
                   {"tileFace", ColorToJson(s.theme.tileFace)},
                   {"tileFaceAlt", ColorToJson(s.theme.tileFaceAlt)},
                   {"digitColor", ColorToJson(s.theme.digitColor)},
                   {"labelColor", ColorToJson(s.theme.labelColor)},
                   {"backgroundColor", ColorToJson(s.theme.backgroundColor)},
                   {"cornerRadius", s.theme.cornerRadius},
                   {"fontFamily", ToUtf8(s.theme.fontFamily)}};

    json gradientStops = json::array();
    for (const auto& stop : s.background.gradientStops) gradientStops.push_back(ColorToJson(stop));

    j["background"] = {{"mode", BgModeToStr(s.background.mode)},
                        {"solidColor", ColorToJson(s.background.solidColor)},
                        {"imagePath", ToUtf8(s.background.imagePath)},
                        {"imageScaleMode", ScaleModeToStr(s.background.imageScaleMode)},
                        {"slideshowFolder", ToUtf8(s.background.slideshowFolder)},
                        {"slideshowShuffle", s.background.slideshowShuffle},
                        {"slideshowIntervalSeconds", s.background.slideshowIntervalSeconds},
                        {"slideshowCrossfadeSeconds", s.background.slideshowCrossfadeSeconds},
                        {"videoPath", ToUtf8(s.background.videoPath)},
                        {"videoLoop", s.background.videoLoop},
                        {"videoMuted", s.background.videoMuted},
                        {"gradientStops", gradientStops},
                        {"gradientCycleSeconds", s.background.gradientCycleSeconds},
                        {"blurAmount", s.background.blurAmount},
                        {"brightnessAmount", s.background.brightnessAmount}};

    j["clock"] = {{"hourFormat", s.clock.hourFormat == HourFormatSetting::H12 ? "h12" : "h24"},
                   {"showSeconds", s.clock.showSeconds},
                   {"showDate", s.clock.showDate},
                   {"showWeekday", s.clock.showWeekday},
                   {"localeAware", s.clock.localeAware},
                   {"timezoneKey", ToUtf8(s.clock.timezoneKey)},
                   {"flipDurationSeconds", s.clock.flipDurationSeconds}};

    j["monitor"] = {{"mode", MonitorModeToStr(s.monitor.mode)}};

    j["performance"] = {{"vsync", s.performance.vsync},
                         {"targetFps", s.performance.targetFps},
                         {"sleepWhenHidden", s.performance.sleepWhenHidden}};

    // Write atomically: write to a temp file then rename, so a crash or
    // power loss mid-write can never leave a half-written / corrupt
    // settings.json behind.
    const std::wstring tmpPath = path + L".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out << j.dump(2);
        if (!out.good()) return false;
    }
    return MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

} // namespace fcs::config
