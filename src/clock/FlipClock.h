#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <string>
#include <ctime>
#include "FlipTile.h"

namespace fcs::clock {

using Microsoft::WRL::ComPtr;

enum class HourFormat { H12, H24 };

struct ClockOptions {
    HourFormat hourFormat = HourFormat::H24;
    bool showSeconds = true;
    bool showDate = true;
    bool showWeekday = true;
    bool localeAware = true;
    std::wstring timezoneName; // empty = system local time
    double flipDurationSeconds = 0.30; // 250-350ms, luxurious mid-point default
};

// Top-level clock widget: owns six FlipTile digits (HH MM SS), the colon
// separators, section labels (HOURS/MINUTES/SECONDS), and an optional date
// / weekday line. Handles per-second value diffing so only tiles whose
// digit actually changed animate (e.g. going from 12:59:59 to 13:00:00
// flips five of six digits simultaneously, each independently eased).
class FlipClock {
public:
    void Initialize(ID2D1DeviceContext* ctx, IDWriteFactory* dwrite, const TileStyle& tileStyle,
                     const ClockOptions& options);

    void SetOptions(const ClockOptions& options) { m_options = options; }
    const ClockOptions& Options() const { return m_options; }

    void SetTileStyle(const TileStyle& style);

    // Call once per frame. Internally checks the wall clock and triggers
    // flips on any tile whose digit changed since the previous call.
    void Update();

    // Lays out and draws the clock centered within the given viewport
    // (typically the full screen bounds in DIPs).
    void Draw(const D2D1_RECT_F& viewport);

private:
    void RefreshFromSystemTime();
    void LayoutAndDraw(const D2D1_RECT_F& viewport);
    std::wstring FormatDate(const std::tm& t) const;
    std::wstring FormatWeekday(const std::tm& t) const;

    ID2D1DeviceContext* m_ctx = nullptr;
    IDWriteFactory* m_dwrite = nullptr;
    TileStyle m_tileStyle;
    ClockOptions m_options;

    // Digit tiles, two per field (tens, ones): [0,1]=hours [2,3]=minutes [4,5]=seconds
    std::vector<std::unique_ptr<FlipTile>> m_tiles;

    ComPtr<IDWriteTextFormat> m_labelFormat;
    ComPtr<IDWriteTextFormat> m_dateFormat;
    ComPtr<ID2D1SolidColorBrush> m_labelBrush;
    ComPtr<ID2D1SolidColorBrush> m_colonBrush;
    ComPtr<ID2D1SolidColorBrush> m_dateBrush;

    std::tm m_lastTime{};
    bool m_hasLastTime = false;
};

} // namespace fcs::clock
