#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include "../animation/AnimationClock.h"

namespace fcs::clock {

using Microsoft::WRL::ComPtr;

// Visual style parameters for a tile, sourced from the active theme so the
// tile itself stays theme-agnostic.
struct TileStyle {
    D2D1_COLOR_F faceColor{0.086f, 0.086f, 0.090f, 1.0f};   // dark charcoal card
    D2D1_COLOR_F faceColorAlt{0.071f, 0.071f, 0.075f, 1.0f}; // subtle top/bottom shade split
    D2D1_COLOR_F digitColor{1.0f, 1.0f, 1.0f, 1.0f};
    D2D1_COLOR_F hingeColor{0.0f, 0.0f, 0.0f, 0.55f};
    D2D1_COLOR_F shadowColor{0.0f, 0.0f, 0.0f, 0.45f};
    float cornerRadius = 10.0f;
    float digitFontSize = 128.0f;
    std::wstring fontFamily = L"Segoe UI Semibold";
};

// One split-flap tile displaying a single character (digit 0-9, or ':' for
// a separator, though separators are rendered by FlipClock directly).
// Owns its own animation state: when SetValue() is called with a new
// character, the tile begins a mechanical flip transition from the old
// value to the new one over the configured duration (250-350ms).
//
// The flip is modeled as two independent half-panels:
//   - Top-half "flap" that rotates down from 0deg to 90deg around the
//     horizontal hinge, revealing the back of the flap (still showing the
//     OLD value) via a perspective (3D) transform.
//   - Bottom-half "flap" that rotates up from 90deg to 0deg, revealing the
//     front of the new value's bottom half.
// This is the classic two-stage split-flap illusion: for the first half of
// the animation the upper flap falls away showing the old-bottom/new-top
// seam; for the second half the new upper flap continues to fall into the
// resting position while a highlight/shadow gradient sells the fold.
class FlipTile {
public:
    FlipTile() = default;

    void Initialize(ID2D1DeviceContext* ctx, IDWriteFactory* dwrite, const TileStyle& style);

    // Instantly sets the tile's displayed value with no animation (used on
    // first paint / after a discontinuity such as timezone change).
    void SetValueImmediate(wchar_t value);

    // Begins (or, if a flip is already running, queues) an animated
    // transition to the new value. durationSeconds ~ 0.25-0.35.
    void SetValue(wchar_t value, double durationSeconds, double nowSeconds);

    // Advances internal animation state; call once per frame before Draw.
    void Update(double nowSeconds);

    // Draws the tile into the device context at the given rect (in DIPs).
    void Draw(const D2D1_RECT_F& bounds);

    bool IsAnimating() const { return m_animating; }
    wchar_t CurrentValue() const { return m_currentValue; }

private:
    void EnsureBrushes();
    void DrawStaticHalf(const D2D1_RECT_F& bounds, wchar_t glyph, bool topHalf,
                         const D2D1_COLOR_F& shade);
    void DrawFoldingHalf(const D2D1_RECT_F& bounds, wchar_t topGlyph, wchar_t bottomGlyph,
                          float progress);
    void DrawGlyphHalf(const D2D1_RECT_F& fullBounds, wchar_t glyph, bool topHalf);

    ID2D1DeviceContext* m_ctx = nullptr; // non-owning, lifetime owned by D2DRenderer
    IDWriteFactory* m_dwrite = nullptr;  // non-owning
    TileStyle m_style;

    ComPtr<IDWriteTextFormat> m_textFormat;
    ComPtr<ID2D1SolidColorBrush> m_faceBrush;
    ComPtr<ID2D1SolidColorBrush> m_faceAltBrush;
    ComPtr<ID2D1SolidColorBrush> m_digitBrush;
    ComPtr<ID2D1SolidColorBrush> m_hingeBrush;
    ComPtr<ID2D1SolidColorBrush> m_shadowBrush;

    wchar_t m_currentValue = L'0';
    wchar_t m_previousValue = L'0';
    bool m_animating = false;

    fcs::animation::Transition m_transition;
    float m_progress = 1.0f; // cached eased progress from the last Update() call
};

} // namespace fcs::clock
