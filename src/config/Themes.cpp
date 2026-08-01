#include "Themes.h"

namespace fcs::config {

namespace {

ThemeSetting MakeTheme(const wchar_t* name, ColorSetting face, ColorSetting faceAlt,
                        ColorSetting digit, ColorSetting label, ColorSetting bg, float radius,
                        const wchar_t* font) {
    ThemeSetting t;
    t.name = name;
    t.tileFace = face;
    t.tileFaceAlt = faceAlt;
    t.digitColor = digit;
    t.labelColor = label;
    t.backgroundColor = bg;
    t.cornerRadius = radius;
    t.fontFamily = font;
    return t;
}

std::vector<ThemeSetting> BuildThemes() {
    std::vector<ThemeSetting> themes;

    // Minimal Black: the default. Soft charcoal tiles on pure black.
    themes.push_back(MakeTheme(L"Minimal Black", {0.086f, 0.086f, 0.090f, 1.0f},
                                {0.071f, 0.071f, 0.075f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f},
                                {0.55f, 0.55f, 0.58f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 10.0f,
                                L"Segoe UI Semibold"));

    // AMOLED: true black everywhere, including the tile faces, to maximize
    // per-pixel power savings on OLED panels; only the digits carry light.
    themes.push_back(MakeTheme(L"AMOLED", {0.0f, 0.0f, 0.0f, 1.0f}, {0.01f, 0.01f, 0.01f, 1.0f},
                                {1.0f, 1.0f, 1.0f, 1.0f}, {0.4f, 0.4f, 0.4f, 1.0f},
                                {0.0f, 0.0f, 0.0f, 1.0f}, 8.0f, L"Segoe UI Semibold"));

    // Glass: translucent, cool-toned tiles suggesting frosted glass over
    // whatever background is showing through (rendered with lower alpha).
    themes.push_back(MakeTheme(L"Glass", {0.5f, 0.55f, 0.62f, 0.28f}, {0.42f, 0.47f, 0.55f, 0.24f},
                                {1.0f, 1.0f, 1.0f, 1.0f}, {0.75f, 0.8f, 0.9f, 0.8f},
                                {0.02f, 0.03f, 0.05f, 1.0f}, 16.0f, L"Segoe UI"));

    // Dark Gray: a neutral, slightly warmer alternative to Minimal Black.
    themes.push_back(MakeTheme(L"Dark Gray", {0.15f, 0.15f, 0.15f, 1.0f}, {0.12f, 0.12f, 0.12f, 1.0f},
                                {0.95f, 0.95f, 0.95f, 1.0f}, {0.55f, 0.55f, 0.55f, 1.0f},
                                {0.06f, 0.06f, 0.06f, 1.0f}, 10.0f, L"Segoe UI Semibold"));

    // Wood: warm amber/brown tones evoking a wooden split-flap board.
    themes.push_back(MakeTheme(L"Wood", {0.28f, 0.18f, 0.11f, 1.0f}, {0.22f, 0.14f, 0.08f, 1.0f},
                                {0.96f, 0.86f, 0.68f, 1.0f}, {0.7f, 0.55f, 0.4f, 1.0f},
                                {0.05f, 0.03f, 0.02f, 1.0f}, 4.0f, L"Georgia"));

    // Retro Flip: classic cream-on-black airport/train-station split-flap
    // look, square corners, high-contrast.
    themes.push_back(MakeTheme(L"Retro Flip", {0.08f, 0.08f, 0.08f, 1.0f}, {0.05f, 0.05f, 0.05f, 1.0f},
                                {0.96f, 0.93f, 0.85f, 1.0f}, {0.6f, 0.58f, 0.52f, 1.0f},
                                {0.0f, 0.0f, 0.0f, 1.0f}, 0.0f, L"Courier New"));

    // Airport Board: amber LED-style digits over deep navy, mimicking a
    // Solari/split-flap departure board.
    themes.push_back(MakeTheme(L"Airport Board", {0.04f, 0.05f, 0.08f, 1.0f}, {0.02f, 0.03f, 0.05f, 1.0f},
                                {1.0f, 0.7f, 0.15f, 1.0f}, {0.5f, 0.4f, 0.2f, 1.0f},
                                {0.01f, 0.01f, 0.03f, 1.0f}, 2.0f, L"Consolas"));

    // Neon: near-black tiles with an electric cyan digit color for a
    // synthwave feel.
    themes.push_back(MakeTheme(L"Neon", {0.05f, 0.05f, 0.08f, 1.0f}, {0.03f, 0.03f, 0.06f, 1.0f},
                                {0.2f, 0.95f, 1.0f, 1.0f}, {0.8f, 0.2f, 0.9f, 1.0f},
                                {0.0f, 0.0f, 0.02f, 1.0f}, 12.0f, L"Segoe UI Semibold"));

    // Mechanical: brushed-steel gray with sharper corners, evoking a
    // physical split-flap mechanism rather than a soft UI card.
    themes.push_back(MakeTheme(L"Mechanical", {0.22f, 0.22f, 0.24f, 1.0f}, {0.17f, 0.17f, 0.19f, 1.0f},
                                {0.92f, 0.92f, 0.94f, 1.0f}, {0.5f, 0.5f, 0.52f, 1.0f},
                                {0.03f, 0.03f, 0.03f, 1.0f}, 3.0f, L"Segoe UI Semibold"));

    return themes;
}

} // namespace

const std::vector<ThemeSetting>& BuiltInThemes() {
    static const std::vector<ThemeSetting> themes = BuildThemes();
    return themes;
}

const ThemeSetting& FindThemeByName(const std::wstring& name) {
    const auto& themes = BuiltInThemes();
    for (const auto& t : themes) {
        if (t.name == name) return t;
    }
    return themes.front();
}

} // namespace fcs::config
