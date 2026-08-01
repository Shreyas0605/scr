#pragma once
#include <windows.h>
#include <ctime>
#include <string>
#include <vector>

namespace fcs::core::TimeZoneUtil {

// Returns the display names of every time zone registered on this machine
// (e.g. "(UTC+05:30) Chennai, Kolkata, Mumbai, New Delhi"), suitable for
// populating the settings dialog's timezone dropdown. Backed by
// EnumDynamicTimeZoneInformation, the real per-machine TZ database rather
// than a hardcoded list, so it stays correct across Windows versions and
// OS updates that add/rename zones.
std::vector<std::wstring> EnumerateDisplayNames();

// Converts a UTC time_t to local wall-clock time in the named time zone
// (matching a display name returned by EnumerateDisplayNames, or a raw TZ
// key such as "India Standard Time"). Falls back to system local time if
// the name is empty or not found. Correctly applies DST rules for the
// zone via GetTimeZoneInformationForYear, so results are accurate for
// historical/future dates too, not just "now".
std::tm ToLocalTimeInZone(std::time_t utcTime, const std::wstring& zoneNameOrKey);

} // namespace fcs::core::TimeZoneUtil
