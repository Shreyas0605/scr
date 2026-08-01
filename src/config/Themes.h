#pragma once
#include <vector>
#include "Settings.h"

namespace fcs::config {

// Returns the built-in theme presets in display order: Minimal Black,
// AMOLED, Glass, Dark Gray, Wood, Retro Flip, Airport Board, Neon,
// Mechanical. Each is a fully-specified ThemeSetting; selecting one in the
// Appearance tab simply copies its fields over the active theme (a "custom"
// theme is just whatever the user has tweaked away from a preset since,
// there being no separate on-disk concept of custom vs. preset - the
// preset name is not force-synced back to "Custom" for simplicity).
const std::vector<ThemeSetting>& BuiltInThemes();

// Looks up a preset by name; returns Minimal Black (index 0) if not found.
const ThemeSetting& FindThemeByName(const std::wstring& name);

} // namespace fcs::config
