#include "TimeZoneUtil.h"
#include <algorithm>

namespace fcs::core::TimeZoneUtil {

namespace {

// Applies a TIME_ZONE_INFORMATION (already resolved for the correct year,
// including DST rule) to convert a UTC SYSTEMTIME to local SYSTEMTIME.
// We implement this manually rather than via the deprecated
// SystemTimeToTzSpecificLocalTime because that API does not accept an
// explicit TIME_ZONE_INFORMATION with a caller-resolved DST rule set for
// an arbitrary year; SystemTimeToTzSpecificLocalTimeEx exists but is
// still tied to "current" DST rules on older SDKs, so we do the bias math
// directly against the year-resolved struct for correctness.
SYSTEMTIME ApplyBias(const SYSTEMTIME& utc, const TIME_ZONE_INFORMATION& tzi, bool isDst) {
    ULARGE_INTEGER uli;
    FILETIME ft;
    SystemTimeToFileTime(&utc, &ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    const LONG biasMinutes = tzi.Bias + (isDst ? tzi.DaylightBias : tzi.StandardBias);
    // FILETIME is in 100ns units; bias is in minutes.
    const long long offset100ns = static_cast<long long>(biasMinutes) * 60LL * 10'000'000LL;
    uli.QuadPart -= static_cast<unsigned long long>(offset100ns);

    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    SYSTEMTIME local{};
    FileTimeToSystemTime(&ft, &local);
    return local;
}

// Determines whether `utc` falls within the zone's DST period for the year
// given a year-resolved TIME_ZONE_INFORMATION (DaylightDate/StandardDate
// populated by GetTimeZoneInformationForYear).
bool IsDaylightTime(const SYSTEMTIME& utc, const TIME_ZONE_INFORMATION& tzi) {
    if (tzi.DaylightDate.wMonth == 0 || tzi.StandardDate.wMonth == 0) {
        return false; // zone has no DST
    }
    // Convert both transition points and `utc` to comparable local-standard
    // time by applying the standard bias, then compare month/day ordering.
    // This is an approximation sufficient for a screensaver clock (exact
    // to the day; sub-day precision at the transition boundary itself is
    // not critical for display purposes).
    SYSTEMTIME localStd = ApplyBias(utc, tzi, false);
    auto key = [](const SYSTEMTIME& s) { return s.wMonth * 100 + s.wDay; };
    const int nowKey = key(localStd);
    const int dstStartKey = tzi.DaylightDate.wMonth * 100 + tzi.DaylightDate.wDay;
    const int dstEndKey = tzi.StandardDate.wMonth * 100 + tzi.StandardDate.wDay;
    if (dstStartKey < dstEndKey) {
        return nowKey >= dstStartKey && nowKey < dstEndKey;
    }
    // Southern-hemisphere style wraparound (DST spans the new year).
    return nowKey >= dstStartKey || nowKey < dstEndKey;
}

bool FindZoneByName(const std::wstring& nameOrKey, DYNAMIC_TIME_ZONE_INFORMATION& outDtzi) {
    DWORD index = 0;
    DYNAMIC_TIME_ZONE_INFORMATION dtzi{};
    while (EnumDynamicTimeZoneInformation(index, &dtzi) == ERROR_SUCCESS) {
        if (nameOrKey == dtzi.TimeZoneKeyName || nameOrKey == dtzi.StandardName) {
            outDtzi = dtzi;
            return true;
        }
        ++index;
    }
    return false;
}

} // namespace

std::vector<std::wstring> EnumerateDisplayNames() {
    std::vector<std::wstring> names;
    DWORD index = 0;
    DYNAMIC_TIME_ZONE_INFORMATION dtzi{};
    while (EnumDynamicTimeZoneInformation(index, &dtzi) == ERROR_SUCCESS) {
        names.emplace_back(dtzi.TimeZoneKeyName);
        ++index;
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::tm ToLocalTimeInZone(std::time_t utcTime, const std::wstring& zoneNameOrKey) {
    std::tm result{};

    if (zoneNameOrKey.empty()) {
        localtime_s(&result, &utcTime);
        return result;
    }

    DYNAMIC_TIME_ZONE_INFORMATION dtzi{};
    if (!FindZoneByName(zoneNameOrKey, dtzi)) {
        // Unknown zone name: fall back to system local time rather than
        // silently producing UTC, since that's the least-surprising
        // fallback for a misconfigured/renamed zone key.
        localtime_s(&result, &utcTime);
        return result;
    }

    SYSTEMTIME utcSt{};
    __int64 t = static_cast<__int64>(utcTime) * 10'000'000LL + 116444736000000000LL;
    FILETIME ft{};
    ft.dwLowDateTime = static_cast<DWORD>(t & 0xFFFFFFFF);
    ft.dwHighDateTime = static_cast<DWORD>(t >> 32);
    FileTimeToSystemTime(&ft, &utcSt);

    TIME_ZONE_INFORMATION tzi{};
    tzi.Bias = dtzi.Bias;
    tzi.DaylightBias = dtzi.DaylightBias;
    tzi.StandardBias = dtzi.StandardBias;
    tzi.DaylightDate = dtzi.DaylightDate;
    tzi.StandardDate = dtzi.StandardDate;
    wcsncpy_s(tzi.DaylightName, dtzi.DaylightName, _TRUNCATE);
    wcsncpy_s(tzi.StandardName, dtzi.StandardName, _TRUNCATE);

    TIME_ZONE_INFORMATION yearTzi = tzi;
    GetTimeZoneInformationForYear(utcSt.wYear, &dtzi, &yearTzi);

    const bool dst = IsDaylightTime(utcSt, yearTzi);
    SYSTEMTIME localSt = ApplyBias(utcSt, yearTzi, dst);

    result.tm_year = localSt.wYear - 1900;
    result.tm_mon = localSt.wMonth - 1;
    result.tm_mday = localSt.wDay;
    result.tm_hour = localSt.wHour;
    result.tm_min = localSt.wMinute;
    result.tm_sec = localSt.wSecond;
    result.tm_wday = localSt.wDayOfWeek;
    return result;
}

} // namespace fcs::core::TimeZoneUtil
