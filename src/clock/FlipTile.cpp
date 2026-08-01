#include "FlipTile.h"
#include "../animation/Easing.h"
#include <cmath>

using namespace fcs::animation;

namespace fcs::clock {

void FlipTile::Initialize(ID2D1DeviceContext* ctx, IDWriteFactory* dwrite, const TileStyle& style) {
    m_ctx = ctx;
    m_dwrite = dwrite;
    m_style = style;

    m_dwrite->CreateTextFormat(m_style.fontFamily.c_str(), nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                m_style.digitFontSize, L"en-us", &m_textFormat);
    if (m_textFormat) {
        m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    EnsureBrushes();
}

void FlipTile::EnsureBrushes() {
    if (!m_ctx) return;
    m_ctx->CreateSolidColorBrush(m_style.faceColor, &m_faceBrush);
    m_ctx->CreateSolidColorBrush(m_style.faceColorAlt, &m_faceAltBrush);
    m_ctx->CreateSolidColorBrush(m_style.digitColor, &m_digitBrush);
    m_ctx->CreateSolidColorBrush(m_style.hingeColor, &m_hingeBrush);
    m_ctx->CreateSolidColorBrush(m_style.shadowColor, &m_shadowBrush);
}

void FlipTile::SetValueImmediate(wchar_t value) {
    m_currentValue = value;
    m_previousValue = value;
    m_animating = false;
    m_transition.Stop();
}

void FlipTile::SetValue(wchar_t value, double durationSeconds, double nowSeconds) {
    if (value == m_currentValue && !m_animating) return;
    m_previousValue = m_currentValue;
    m_currentValue = value;
    m_transition.Start(durationSeconds, nowSeconds);
    m_animating = true;
}

void FlipTile::Update(double nowSeconds) {
    if (m_animating) {
        const float t = m_transition.LinearProgress(nowSeconds);
        m_progress = Easing::CubicInOut(t);
        if (m_transition.IsFinished(nowSeconds)) {
            m_animating = false;
            m_transition.Stop();
        }
    }
}

void FlipTile::DrawGlyphHalf(const D2D1_RECT_F& fullBounds, wchar_t glyph, bool topHalf) {
    // Draw the glyph centered across the *full* tile height into a clipped
    // half, so the digit appears continuous across the two panels (exactly
    // like a real split-flap card where the character spans both leaves).
    const D2D1_RECT_F clip = topHalf
        ? D2D1::RectF(fullBounds.left, fullBounds.top, fullBounds.right,
                       (fullBounds.top + fullBounds.bottom) * 0.5f)
        : D2D1::RectF(fullBounds.left, (fullBounds.top + fullBounds.bottom) * 0.5f,
                       fullBounds.right, fullBounds.bottom);

    m_ctx->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    wchar_t text[2] = {glyph, L'\0'};
    m_ctx->DrawText(text, 1, m_textFormat.Get(), fullBounds, m_digitBrush.Get(),
                     D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);

    m_ctx->PopAxisAlignedClip();
}

void FlipTile::DrawStaticHalf(const D2D1_RECT_F& bounds, wchar_t glyph, bool topHalf,
                               const D2D1_COLOR_F& shade) {
    const float midY = (bounds.top + bounds.bottom) * 0.5f;
    const D2D1_RECT_F half = topHalf
        ? D2D1::RectF(bounds.left, bounds.top, bounds.right, midY)
        : D2D1::RectF(bounds.left, midY, bounds.right, bounds.bottom);

    ComPtr<ID2D1SolidColorBrush> brush;
    m_ctx->CreateSolidColorBrush(shade, &brush);

    // Rounded only on the tile's outer corners (top-left/top-right for the
    // top half, bottom-left/bottom-right for the bottom half), sharp on the
    // hinge seam, matching real split-flap card geometry.
    const float r = m_style.cornerRadius;
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(bounds, r, r);
    m_ctx->PushAxisAlignedClip(half, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_ctx->FillRoundedRectangle(rr, brush.Get());
    m_ctx->PopAxisAlignedClip();

    DrawGlyphHalf(bounds, glyph, topHalf);

    // Hinge seam line.
    ComPtr<ID2D1SolidColorBrush> hingeBrush;
    m_ctx->CreateSolidColorBrush(m_style.hingeColor, &hingeBrush);
    m_ctx->DrawLine(D2D1::Point2F(bounds.left + 2, midY), D2D1::Point2F(bounds.right - 2, midY),
                     hingeBrush.Get(), 1.5f);
}

void FlipTile::DrawFoldingHalf(const D2D1_RECT_F& bounds, wchar_t topGlyph, wchar_t bottomGlyph,
                                float progress) {
    // progress in [0,1]: 0 = flap flat against top (angle 0deg from viewer),
    // 1 = flap flat against bottom (angle 180deg, i.e. fully folded down).
    // We approximate the perspective foreshortening of a hinge rotating
    // about the horizontal midline by scaling the flap's vertical extent by
    // cos(angle) and shading it darker as it turns edge-on (angle -> 90deg),
    // which is the standard 2D approximation used by split-flap renderers
    // since true 3D perspective isn't exposed by the 2D transform stack.
    const float angle = progress * 3.14159265f; // 0..pi (0..180deg)
    const float midY = (bounds.top + bounds.bottom) * 0.5f;
    const float halfH = (bounds.bottom - bounds.top) * 0.5f;

    const bool foldingFromTop = progress <= 0.5001f; // first half: top flap folds down
    const float localProgress = foldingFromTop ? progress * 2.0f : (progress - 0.5f) * 2.0f;
    const float localAngle = localProgress * 1.57079633f; // 0..90deg within this half-stage

    const float scaleY = std::cos(localAngle); // foreshortening as it rotates edge-on
    const float shade = 0.35f + 0.65f * std::fabs(scaleY); // darker near 90deg (edge-on)

    D2D1_COLOR_F flapColor = m_style.faceColor;
    flapColor.r *= shade;
    flapColor.g *= shade;
    flapColor.b *= shade;

    ComPtr<ID2D1SolidColorBrush> flapBrush;
    m_ctx->CreateSolidColorBrush(flapColor, &flapBrush);

    D2D1_MATRIX_3X2_F oldTransform;
    m_ctx->GetTransform(&oldTransform);

    if (foldingFromTop) {
        // Top flap (showing the OLD top glyph) rotates down from the hinge,
        // scaling toward zero height as it becomes edge-on.
        const D2D1_RECT_F flapRect = D2D1::RectF(bounds.left, midY - halfH, bounds.right, midY);
        const D2D1_MATRIX_3X2_F scale =
            D2D1::Matrix3x2F::Scale(1.0f, std::max(scaleY, 0.02f), D2D1::Point2F((bounds.left + bounds.right) * 0.5f, midY));
        m_ctx->SetTransform(scale * oldTransform);

        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(bounds, m_style.cornerRadius, m_style.cornerRadius);
        m_ctx->PushAxisAlignedClip(flapRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        m_ctx->FillRoundedRectangle(rr, flapBrush.Get());
        DrawGlyphHalf(bounds, topGlyph, true);
        m_ctx->PopAxisAlignedClip();
        m_ctx->SetTransform(oldTransform);

        // Bottom half stays static, already showing the NEW bottom glyph
        // (real split-flap cards reveal the new bottom immediately since
        // it's a separate physical leaf).
        DrawStaticHalf(bounds, bottomGlyph, false, m_style.faceColorAlt);
    } else {
        // Top half is now static, showing the NEW top glyph.
        DrawStaticHalf(bounds, topGlyph, true, m_style.faceColor);

        // Bottom flap rotates up into resting position, revealing the NEW
        // bottom glyph as it un-folds (scaleY goes 0.02 -> 1.0).
        const float unfold = 1.0f - localAngle / 1.57079633f; // 1 -> 0 over this stage... invert:
        const float scaleUp = std::sin(localAngle);            // 0 -> 1 as it settles flat
        const D2D1_RECT_F flapRect = D2D1::RectF(bounds.left, midY, bounds.right, midY + halfH);
        const D2D1_MATRIX_3X2_F scale =
            D2D1::Matrix3x2F::Scale(1.0f, std::max(scaleUp, 0.02f), D2D1::Point2F((bounds.left + bounds.right) * 0.5f, midY));
        m_ctx->SetTransform(scale * oldTransform);

        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(bounds, m_style.cornerRadius, m_style.cornerRadius);
        m_ctx->PushAxisAlignedClip(flapRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        m_ctx->FillRoundedRectangle(rr, flapBrush.Get());
        DrawGlyphHalf(bounds, bottomGlyph, false);
        m_ctx->PopAxisAlignedClip();
        m_ctx->SetTransform(oldTransform);
        (void)unfold;
        (void)angle;
    }

    // Cast a soft contact shadow from the folding flap onto the panel
    // beneath it, strongest when the flap is near edge-on (mid-flip).
    const float shadowStrength = std::sin(localAngle) * 0.5f;
    if (shadowStrength > 0.02f) {
        D2D1_COLOR_F sc = m_style.shadowColor;
        sc.a = shadowStrength;
        ComPtr<ID2D1SolidColorBrush> shadowBrush;
        m_ctx->CreateSolidColorBrush(sc, &shadowBrush);
        const float shadowH = halfH * 0.18f;
        const D2D1_RECT_F shadowRect = foldingFromTop
            ? D2D1::RectF(bounds.left, midY, bounds.right, midY + shadowH)
            : D2D1::RectF(bounds.left, midY - shadowH, bounds.right, midY);
        m_ctx->FillRectangle(shadowRect, shadowBrush.Get());
    }
}

void FlipTile::Draw(const D2D1_RECT_F& bounds) {
    if (!m_ctx || !m_textFormat) return;

    // Drop shadow beneath the whole tile card.
    D2D1_RECT_F shadowRect = bounds;
    shadowRect.top += 3.0f;
    shadowRect.bottom += 5.0f;
    D2D1_ROUNDED_RECT sr = D2D1::RoundedRect(shadowRect, m_style.cornerRadius, m_style.cornerRadius);
    m_ctx->FillRoundedRectangle(sr, m_shadowBrush.Get());

    if (!m_animating) {
        DrawStaticHalf(bounds, m_currentValue, true, m_style.faceColor);
        DrawStaticHalf(bounds, m_currentValue, false, m_style.faceColorAlt);
        return;
    }

    DrawFoldingHalf(bounds, m_previousValue, m_currentValue, m_progress);
}

} // namespace fcs::clock
