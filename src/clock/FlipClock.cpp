#include "FlipClock.h"
#include "../core/TimeZoneUtil.h"
#include "../animation/AnimationClock.h"
#include <array>
#include <sstream>
#include <iomanip>
#include <algorithm>

using fcs::animation::AnimationClock;

namespace fcs::clock {

namespace {
const wchar_t* kMonthNames[] = {L"January", L"February", L"March",     L"April",   L"May",
                                 L"June",    L"July",     L"August",    L"September",
                                 L"October", L"November", L"December"};
const wchar_t* kWeekdayNames[] = {L"SUNDAY",   L"MONDAY", L"TUESDAY",  L"WEDNESDAY",
                                   L"THURSDAY", L"FRIDAY", L"SATURDAY"};
} // namespace

void FlipClock::Initialize(ID2D1DeviceContext* ctx, IDWriteFactory* dwrite,
                            const TileStyle& tileStyle, const ClockOptions& options) {
    m_ctx = ctx;
    m_dwrite = dwrite;
    m_tileStyle = tileStyle;
    m_options = options;

    m_tiles.clear();
    for (int i = 0; i < 6; ++i) {
        auto tile = std::make_unique<FlipTile>();
        tile->Initialize(ctx, dwrite, tileStyle);
        tile->SetValueImmediate(L'0');
        m_tiles.push_back(std::move(tile));
    }

    m_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f,
                                L"en-us", &m_labelFormat);
    if (m_labelFormat) {
        m_labelFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_labelFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    m_dwrite->CreateTextFormat(L"Segoe UI Light", nullptr, DWRITE_FONT_WEIGHT_LIGHT,
                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 24.0f,
                                L"en-us", &m_dateFormat);
    if (m_dateFormat) {
        m_dateFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_dateFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    ctx->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.55f, 0.58f, 1.0f), &m_labelBrush);
    ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.9f), &m_colonBrush);
    ctx->CreateSolidColorBrush(D2D1::ColorF(0.75f, 0.75f, 0.78f, 1.0f), &m_dateBrush);

    m_hasLastTime = false;
}

void FlipClock::SetTileStyle(const TileStyle& style) {
    m_tileStyle = style;
    for (auto& tile : m_tiles) {
        tile->Initialize(m_ctx, m_dwrite, style);
    }
}

void FlipClock::RefreshFromSystemTime() {
    const std::time_t now = std::time(nullptr);
    const std::tm local = fcs::core::TimeZoneUtil::ToLocalTimeInZone(now, m_options.timezoneName);

    int displayHour = local.tm_hour;
    if (m_options.hourFormat == HourFormat::H12) {
        displayHour = displayHour % 12;
        if (displayHour == 0) displayHour = 12;
    }

    const int hourVals[2] = {displayHour / 10, displayHour % 10};
    const int minVals[2] = {local.tm_min / 10, local.tm_min % 10};
    const int secVals[2] = {local.tm_sec / 10, local.tm_sec % 10};
    const int allVals[6] = {hourVals[0], hourVals[1], minVals[0], minVals[1], secVals[0], secVals[1]};

    const double now_s = AnimationClock::NowSeconds();
    for (int i = 0; i < 6; ++i) {
        const wchar_t digitChar = static_cast<wchar_t>(L'0' + allVals[i]);
        if (!m_hasLastTime) {
            m_tiles[i]->SetValueImmediate(digitChar);
        } else if (m_tiles[i]->CurrentValue() != digitChar) {
            m_tiles[i]->SetValue(digitChar, m_options.flipDurationSeconds, now_s);
        }
    }

    m_lastTime = local;
    m_hasLastTime = true;
}

void FlipClock::Update() {
    RefreshFromSystemTime();
    const double now_s = AnimationClock::NowSeconds();
    for (auto& tile : m_tiles) tile->Update(now_s);
}

std::wstring FlipClock::FormatDate(const std::tm& t) const {
    std::wstringstream ss;
    ss << kMonthNames[std::clamp(t.tm_mon, 0, 11)] << L" " << t.tm_mday << L", " << (t.tm_year + 1900);
    return ss.str();
}

std::wstring FlipClock::FormatWeekday(const std::tm& t) const {
    return kWeekdayNames[std::clamp(t.tm_wday, 0, 6)];
}

void FlipClock::Draw(const D2D1_RECT_F& viewport) {
    if (m_tiles.size() != 6) return;

    m_ctx->SetTransform(D2D1::Matrix3x2F::Identity());

    LayoutAndDraw(viewport);
}

void FlipClock::LayoutAndDraw(const D2D1_RECT_F& viewport) {
    const float viewW = viewport.right - viewport.left;
    const float viewH = viewport.bottom - viewport.top;

    // Tile sizing: derived from viewport so the clock scales cleanly from
    // 1080p through 8K and ultrawide/portrait without hardcoded pixels.
    const int fieldCount = m_options.showSeconds ? 3 : 2;
    const float tileWidthRatio = 0.72f;
    const float tileGapRatio = 0.06f;
    const float colonWidthRatio = 0.28f;
    const float fieldGapRatio = 0.22f;
    const float widthBudgetRatio = 0.92f;

    const float totalWidthRatio =
        fieldCount * (tileWidthRatio * 2.0f + tileGapRatio) +
        (fieldCount - 1) * (colonWidthRatio + fieldGapRatio * 2.0f);
    const float maxTileHByWidth = (viewW * widthBudgetRatio) / totalWidthRatio;
    const float tileH = std::min(viewH * 0.32f, maxTileHByWidth);
    const float tileW = tileH * tileWidthRatio;
    const float tileGapWithinField = tileH * tileGapRatio;
    const float colonWidth = tileH * colonWidthRatio;
    const float fieldGap = tileH * fieldGapRatio;

    const float fieldWidth = tileW * 2 + tileGapWithinField;
    const float totalWidth =
        fieldWidth * fieldCount + colonWidth * (fieldCount - 1) + fieldGap * 2 * (fieldCount - 1);

    const float labelHeight = 28.0f;
    const float dateHeight = (m_options.showDate || m_options.showWeekday) ? 60.0f : 0.0f;
    const float totalHeight = tileH + labelHeight + 24.0f + dateHeight;

    float startX = viewport.left + (viewW - totalWidth) * 0.5f;
    const float startY = viewport.top + (viewH - totalHeight) * 0.5f;

    struct FieldInfo {
        int tileIndexBase;
        const wchar_t* label;
    };
    std::array<FieldInfo, 3> fields = {
        FieldInfo{0, L"HOURS"}, FieldInfo{2, L"MINUTES"}, FieldInfo{4, L"SECONDS"}};

    float x = startX;
    for (int f = 0; f < fieldCount; ++f) {
        const FieldInfo& field = fields[f];

        for (int d = 0; d < 2; ++d) {
            D2D1_RECT_F tileRect =
                D2D1::RectF(x, startY, x + tileW, startY + tileH);
            m_tiles[field.tileIndexBase + d]->Draw(tileRect);
            x += tileW;
            if (d == 0) x += tileGapWithinField;
        }

        // Label beneath this field, centered under its two tiles.
        D2D1_RECT_F labelRect =
            D2D1::RectF(x - fieldWidth, startY + tileH + 10.0f, x, startY + tileH + 10.0f + labelHeight);
        m_ctx->DrawText(field.label, static_cast<UINT32>(wcslen(field.label)), m_labelFormat.Get(),
                         labelRect, m_labelBrush.Get());

        // Colon separator (skip after the last field).
        if (f < fieldCount - 1) {
            x += fieldGap;
            const float colonCx = x + colonWidth * 0.5f;
            const float dotR = tileH * 0.035f;
            m_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(colonCx, startY + tileH * 0.36f), dotR, dotR),
                                m_colonBrush.Get());
            m_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(colonCx, startY + tileH * 0.64f), dotR, dotR),
                                m_colonBrush.Get());
            x += colonWidth + fieldGap;
        }
    }

    if (dateHeight > 0.0f) {
        std::wstring dateLine;
        if (m_options.showWeekday) dateLine += FormatWeekday(m_lastTime);
        if (m_options.showWeekday && m_options.showDate) dateLine += L"   \u2022   ";
        if (m_options.showDate) dateLine += FormatDate(m_lastTime);

        D2D1_RECT_F dateRect = D2D1::RectF(viewport.left, startY + tileH + labelHeight + 24.0f,
                                            viewport.right, startY + totalHeight);
        m_ctx->DrawText(dateLine.c_str(), static_cast<UINT32>(dateLine.size()), m_dateFormat.Get(),
                         dateRect, m_dateBrush.Get());
    }
}

} // namespace fcs::clock
