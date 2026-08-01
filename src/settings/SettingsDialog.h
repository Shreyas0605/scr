#pragma once
#include <windows.h>
#include "../config/Settings.h"

namespace fcs::settings {

// Shows the modal, tabbed settings/configuration window (the target of the
// `/c` and `/c:<hwnd>` screensaver contract). Blocks until the user closes
// it. Persists changes to disk via fcs::config::SaveSettings on OK/Apply;
// `settings` is updated in place so a caller sharing the same process (e.g.
// a future "live preview while configuring" mode) sees the latest values.
//
// `parentHwnd` is the HWND Windows passes after `/c:` when launched from
// the Display Settings "Settings..." button; may be nullptr when launched
// standalone (e.g. via right-click "Configure" in Explorer), in which case
// the dialog is simply not owned by another window.
void ShowSettingsDialog(HINSTANCE hInstance, HWND parentHwnd, fcs::config::Settings& settings);

} // namespace fcs::settings
