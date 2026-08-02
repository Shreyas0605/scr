// d2d1effects.h (included via BackgroundManager.h) declares CLSID_D2D1GaussianBlur
// and CLSID_D2D1Brightness as `extern const GUID` - no .lib provides storage for
// them. Defining INITGUID before the include causes DEFINE_GUID to actually
// instantiate them here instead. This is the only translation unit that
// references these two CLSIDs, so there's no duplicate-definition risk.
#define INITGUID
#include "BackgroundManager.h"
#include "ImageLoader.h"
#include "../animation/Easing.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;
using namespace fcs::animation; // brings the Easing namespace + AnimationClock into scope

namespace fcs::background {

namespace {
D2D1_COLOR_F ToD2D(const fcs::config::ColorSetting& c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }

bool HasImageExtension(const fs::path& p) {
    static const std::vector<std::wstring> exts = {L".png", L".jpg",  L".jpeg", L".webp",
                                                     L".bmp", L".heic", L".heif", L".tiff"};
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}
} // namespace

void BackgroundManager::Initialize(ID2D1DeviceContext* ctx, IWICImagingFactory* wic) {
    m_ctx = ctx;
    m_wic = wic;
    m_video = std::make_unique<VideoBackground>();
    RebuildEffectChain();
}

void BackgroundManager::RebuildEffectChain() {
    if (!m_ctx) return;
    m_ctx->CreateEffect(CLSID_D2D1GaussianBlur, &m_blurEffect);
    if (m_blurEffect) {
        m_blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);
        m_blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    }
    m_ctx->CreateEffect(CLSID_D2D1Brightness, &m_brightnessEffect);
}

void BackgroundManager::ApplySettings(const fcs::config::BackgroundSettings& settings) {
    const bool imageChanged = settings.imagePath != m_settings.imagePath;
    const bool slideshowChanged = settings.slideshowFolder != m_settings.slideshowFolder;
    const bool videoChanged =
        settings.videoPath != m_settings.videoPath || settings.videoLoop != m_settings.videoLoop ||
        settings.videoMuted != m_settings.videoMuted;

    m_settings = settings;

    if (imageChanged || (!m_imageBitmap && settings.mode == fcs::config::BackgroundMode::Image)) {
        m_needsImageReload = true;
    }
    if (slideshowChanged ||
        (m_slideshowFiles.empty() && settings.mode == fcs::config::BackgroundMode::Slideshow)) {
        m_needsSlideshowReload = true;
    }
    if (videoChanged || (!m_video->IsOpen() && settings.mode == fcs::config::BackgroundMode::Video)) {
        m_needsVideoReload = true;
    }
}

void BackgroundManager::ReloadImage() {
    m_needsImageReload = false;
    if (m_settings.imagePath.empty()) {
        m_imageBitmap.Reset();
        return;
    }
    m_imageBitmap = ImageLoader::LoadToBitmap(m_wic, m_ctx, m_settings.imagePath);
}

void BackgroundManager::ReloadSlideshow() {
    m_needsSlideshowReload = false;
    m_slideshowFiles.clear();
    m_slideshowIndex = 0;
    m_slideshowCurrent.Reset();
    m_slideshowNext.Reset();
    m_slideshowCrossfading = false;
    m_slideshowElapsed = 0.0;

    if (m_settings.slideshowFolder.empty()) return;

    std::error_code ec;
    if (!fs::exists(m_settings.slideshowFolder, ec) || ec) return;

    for (const auto& entry : fs::directory_iterator(m_settings.slideshowFolder, ec)) {
        if (ec) break;
        if (entry.is_regular_file() && HasImageExtension(entry.path())) {
            m_slideshowFiles.push_back(entry.path().wstring());
        }
    }
    if (m_slideshowFiles.empty()) return;

    if (m_settings.slideshowShuffle) {
        std::shuffle(m_slideshowFiles.begin(), m_slideshowFiles.end(), m_rng);
    } else {
        std::sort(m_slideshowFiles.begin(), m_slideshowFiles.end());
    }

    m_slideshowCurrent = ImageLoader::LoadToBitmap(m_wic, m_ctx, m_slideshowFiles[0]);
}

void BackgroundManager::ReloadVideo() {
    m_needsVideoReload = false;
    m_video->Shutdown();
    if (m_settings.videoPath.empty()) return;
    m_video->Open(m_ctx, m_settings.videoPath, m_settings.videoLoop, m_settings.videoMuted);
}

void BackgroundManager::AdvanceSlideshow() {
    if (m_slideshowFiles.size() < 2) return;

    const double dt = 1.0 / 60.0; // Update() called once per frame at render cadence
    if (m_slideshowCrossfading) {
        m_crossfadeElapsed += dt;
        if (m_crossfadeElapsed >= m_settings.slideshowCrossfadeSeconds) {
            m_slideshowCurrent = m_slideshowNext;
            m_slideshowNext.Reset();
            m_slideshowCrossfading = false;
            m_slideshowElapsed = 0.0;
        }
        return;
    }

    m_slideshowElapsed += dt;
    if (m_slideshowElapsed >= m_settings.slideshowIntervalSeconds) {
        m_slideshowIndex = (m_slideshowIndex + 1) % m_slideshowFiles.size();
        m_slideshowNext = ImageLoader::LoadToBitmap(m_wic, m_ctx, m_slideshowFiles[m_slideshowIndex]);
        m_slideshowCrossfading = true;
        m_crossfadeElapsed = 0.0;
    }
}

void BackgroundManager::Update() {
    if (m_needsImageReload) ReloadImage();
    if (m_needsSlideshowReload) ReloadSlideshow();
    if (m_needsVideoReload) ReloadVideo();

    if (m_settings.mode == fcs::config::BackgroundMode::Slideshow) {
        AdvanceSlideshow();
    }
}

void BackgroundManager::DrawBitmapCover(ID2D1Bitmap1* bitmap, const D2D1_RECT_F& viewport,
                                         fcs::config::ScaleMode scaleMode, float opacity) {
    if (!bitmap) return;
    const D2D1_SIZE_F bmpSize = bitmap->GetSize();
    const float viewW = viewport.right - viewport.left;
    const float viewH = viewport.bottom - viewport.top;
    if (bmpSize.width <= 0 || bmpSize.height <= 0 || viewW <= 0 || viewH <= 0) return;

    D2D1_RECT_F destRect = viewport;
    D2D1_RECT_F srcRect = D2D1::RectF(0, 0, bmpSize.width, bmpSize.height);

    switch (scaleMode) {
        case fcs::config::ScaleMode::Stretch: {
            destRect = viewport;
            break;
        }
        case fcs::config::ScaleMode::Center: {
            const float x = viewport.left + (viewW - bmpSize.width) * 0.5f;
            const float y = viewport.top + (viewH - bmpSize.height) * 0.5f;
            destRect = D2D1::RectF(x, y, x + bmpSize.width, y + bmpSize.height);
            break;
        }
        case fcs::config::ScaleMode::Tile: {
            ComPtr<ID2D1BitmapBrush> tileBrush;
            m_ctx->CreateBitmapBrush(
                bitmap,
                D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP),
                D2D1::BrushProperties(opacity), &tileBrush);
            if (tileBrush) m_ctx->FillRectangle(viewport, tileBrush.Get());
            return;
        }
        case fcs::config::ScaleMode::Fit: {
            const float scale = std::min(viewW / bmpSize.width, viewH / bmpSize.height);
            const float w = bmpSize.width * scale, h = bmpSize.height * scale;
            const float x = viewport.left + (viewW - w) * 0.5f;
            const float y = viewport.top + (viewH - h) * 0.5f;
            destRect = D2D1::RectF(x, y, x + w, y + h);
            break;
        }
        case fcs::config::ScaleMode::Fill:
        default: {
            // Cover: scale to fill viewport, cropping overflow via source
            // rect rather than stretching, matching CSS background-size:cover.
            const float scale = std::max(viewW / bmpSize.width, viewH / bmpSize.height);
            const float srcW = viewW / scale, srcH = viewH / scale;
            const float srcX = (bmpSize.width - srcW) * 0.5f;
            const float srcY = (bmpSize.height - srcH) * 0.5f;
            srcRect = D2D1::RectF(srcX, srcY, srcX + srcW, srcY + srcH);
            destRect = viewport;
            break;
        }
    }

    m_ctx->DrawBitmap(bitmap, destRect, opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, srcRect);
}

void BackgroundManager::DrawSolid(const D2D1_RECT_F& viewport) {
    ComPtr<ID2D1SolidColorBrush> brush;
    m_ctx->CreateSolidColorBrush(ToD2D(m_settings.solidColor), &brush);
    if (brush) m_ctx->FillRectangle(viewport, brush.Get());
}

void BackgroundManager::DrawGradient(const D2D1_RECT_F& viewport) {
    if (m_settings.gradientStops.empty()) {
        DrawSolid(viewport);
        return;
    }

    std::vector<D2D1_GRADIENT_STOP> stops;
    const size_t n = m_settings.gradientStops.size();
    for (size_t i = 0; i < n; ++i) {
        D2D1_GRADIENT_STOP gs;
        gs.position = n > 1 ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.0f;
        gs.color = ToD2D(m_settings.gradientStops[i]);
        stops.push_back(gs);
    }

    ComPtr<ID2D1GradientStopCollection> stopCollection;
    m_ctx->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(stops.size()),
                                         &stopCollection);
    if (!stopCollection) return;

    // Slowly rotate the gradient axis around the viewport center over
    // gradientCycleSeconds for a subtle "living wallpaper" animated feel.
    const double t = AnimationClock::NowSeconds();
    const double cyclePhase =
        m_settings.gradientCycleSeconds > 0.0
            ? std::fmod(t, m_settings.gradientCycleSeconds) / m_settings.gradientCycleSeconds
            : 0.0;
    const float angle = static_cast<float>(cyclePhase * 2.0 * 3.14159265358979323846);

    const float cx = (viewport.left + viewport.right) * 0.5f;
    const float cy = (viewport.top + viewport.bottom) * 0.5f;
    const float radius = std::max(viewport.right - viewport.left, viewport.bottom - viewport.top) * 0.75f;
    const D2D1_POINT_2F start = D2D1::Point2F(cx + radius * std::cos(angle), cy + radius * std::sin(angle));
    const D2D1_POINT_2F end =
        D2D1::Point2F(cx - radius * std::cos(angle), cy - radius * std::sin(angle));

    ComPtr<ID2D1LinearGradientBrush> gradBrush;
    m_ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(start, end),
                                      stopCollection.Get(), &gradBrush);
    if (gradBrush) m_ctx->FillRectangle(viewport, gradBrush.Get());
}

void BackgroundManager::Draw(const D2D1_RECT_F& viewport) {
    using fcs::config::BackgroundMode;

    // Gradient and solid color are drawn directly with brushes; only
    // bitmap-backed modes (image/slideshow/video) go through the
    // blur+brightness effect pipeline, since a procedural gradient has no
    // "source pixels" to blur and dimming it is just alpha compositing.
    if (m_settings.mode == BackgroundMode::SolidColor) {
        DrawSolid(viewport);
        return;
    }
    if (m_settings.mode == BackgroundMode::AnimatedGradient) {
        DrawGradient(viewport);
        return;
    }

    const bool needsEffects = m_settings.blurAmount > 0 || m_settings.brightnessAmount != 100;

    if (!needsEffects) {
        DrawSolid(viewport); // clear to black first to avoid ghosting on transparent edges
        if (m_settings.mode == BackgroundMode::Image) {
            DrawBitmapCover(m_imageBitmap.Get(), viewport, m_settings.imageScaleMode, 1.0f);
        } else if (m_settings.mode == BackgroundMode::Slideshow) {
            if (m_slideshowCrossfading && m_slideshowNext) {
                const float t = static_cast<float>(std::clamp(
                    m_crossfadeElapsed / std::max(m_settings.slideshowCrossfadeSeconds, 0.001), 0.0, 1.0));
                const float eased = Easing::SineInOut(t);
                DrawBitmapCover(m_slideshowCurrent.Get(), viewport, m_settings.imageScaleMode, 1.0f - eased);
                DrawBitmapCover(m_slideshowNext.Get(), viewport, m_settings.imageScaleMode, eased);
            } else {
                DrawBitmapCover(m_slideshowCurrent.Get(), viewport, m_settings.imageScaleMode, 1.0f);
            }
        } else if (m_settings.mode == BackgroundMode::Video) {
            DrawBitmapCover(m_video->CurrentFrame().Get(), viewport, fcs::config::ScaleMode::Fill, 1.0f);
        }
        return;
    }

    // --- Effect pipeline path: compose the raw background into an
    // offscreen target sized to the viewport, then pipe it through
    // Gaussian blur and/or brightness before compositing to the swap
    // chain. This is genuine GPU pixel-shader blur (D2D1 built-in effect),
    // not a pre-rendered or faked approximation. ---
    //
    // The compose bitmap must be sized in real physical pixels and tagged
    // with the context's actual DPI - not the viewport's DIP extent used
    // directly as a pixel count - or drawing into it at the context's
    // current DPI overflows its actual pixel buffer and clips exactly like
    // the outer-viewport bug this mirrors (see D2DRenderer::LogicalSize).
    float dpiX = 96.0f, dpiY = 96.0f;
    m_ctx->GetDpi(&dpiX, &dpiY);
    const float viewW = viewport.right - viewport.left;
    const float viewH = viewport.bottom - viewport.top;
    const UINT32 w = static_cast<UINT32>(std::max(1.0f, viewW * dpiX / 96.0f));
    const UINT32 h = static_cast<UINT32>(std::max(1.0f, viewH * dpiY / 96.0f));

    D2D1_BITMAP_PROPERTIES1 composeProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        dpiX, dpiY);
    ComPtr<ID2D1Bitmap1> composeTarget;
    if (FAILED(m_ctx->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, composeProps, &composeTarget)) ||
        !composeTarget) {
        DrawSolid(viewport);
        return;
    }

    // NOTE: this SetTarget switch happens *inside* the caller's single
    // BeginDraw/EndDraw bracket for the frame (D2DRenderer::BeginDraw is
    // called once per frame before any Draw() calls). D2D permits
    // redirecting the device context's target mid-frame; only the outer
    // BeginDraw/EndDraw pair must not be nested, and it isn't here.
    ComPtr<ID2D1Image> priorTarget;
    m_ctx->GetTarget(&priorTarget);
    m_ctx->SetTarget(composeTarget.Get());
    const D2D1_RECT_F localViewport = D2D1::RectF(0.0f, 0.0f, viewW, viewH);
    DrawSolid(localViewport);
    if (m_settings.mode == BackgroundMode::Image) {
        DrawBitmapCover(m_imageBitmap.Get(), localViewport, m_settings.imageScaleMode, 1.0f);
    } else if (m_settings.mode == BackgroundMode::Slideshow) {
        if (m_slideshowCrossfading && m_slideshowNext) {
            const float t = static_cast<float>(std::clamp(
                m_crossfadeElapsed / std::max(m_settings.slideshowCrossfadeSeconds, 0.001), 0.0, 1.0));
            const float eased = Easing::SineInOut(t);
            DrawBitmapCover(m_slideshowCurrent.Get(), localViewport, m_settings.imageScaleMode, 1.0f - eased);
            DrawBitmapCover(m_slideshowNext.Get(), localViewport, m_settings.imageScaleMode, eased);
        } else {
            DrawBitmapCover(m_slideshowCurrent.Get(), localViewport, m_settings.imageScaleMode, 1.0f);
        }
    } else if (m_settings.mode == BackgroundMode::Video) {
        DrawBitmapCover(m_video->CurrentFrame().Get(), localViewport, fcs::config::ScaleMode::Fill, 1.0f);
    }
    m_ctx->SetTarget(priorTarget.Get());

    ID2D1Image* pipelineSource = composeTarget.Get();

    if (m_settings.blurAmount > 0 && m_blurEffect) {
        // Map slider 0-100 to a standard deviation of 0-30 DIPs, giving a
        // visible-but-controllable soft blur across the full range.
        const float stdDev = (m_settings.blurAmount / 100.0f) * 30.0f;
        m_blurEffect->SetInput(0, pipelineSource);
        m_blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, stdDev);
        ComPtr<ID2D1Image> blurOut;
        m_blurEffect->GetOutput(&blurOut);
        pipelineSource = blurOut.Get();
    }

    ComPtr<ID2D1Image> keepAlive; // holds blur output alive across the branch below
    if (m_settings.brightnessAmount != 100 && m_brightnessEffect) {
        const float b = std::clamp(m_settings.brightnessAmount / 100.0f, 0.0f, 1.0f);
        // Compress the white point down toward black to dim the image
        // while leaving black levels anchored at 0, matching a simple
        // "brightness slider" perceptual model.
        m_brightnessEffect->SetInput(0, pipelineSource);
        m_brightnessEffect->SetValue(D2D1_BRIGHTNESS_PROP_WHITE_POINT, D2D1::Vector2F(1.0f, b));
        m_brightnessEffect->SetValue(D2D1_BRIGHTNESS_PROP_BLACK_POINT, D2D1::Vector2F(0.0f, 0.0f));
        m_ctx->DrawImage(m_brightnessEffect.Get(), D2D1::Point2F(viewport.left, viewport.top));
    } else {
        m_ctx->DrawImage(pipelineSource, D2D1::Point2F(viewport.left, viewport.top));
    }
}

} // namespace fcs::background
