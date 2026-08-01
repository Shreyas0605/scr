#pragma once
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <random>
#include "../config/Settings.h"
#include "VideoBackground.h"
#include "../animation/AnimationClock.h"

namespace fcs::background {

using Microsoft::WRL::ComPtr;

// Owns and renders whichever background the active settings select:
// solid color, still image, slideshow (with crossfade), looping video, or
// an animated gradient. Real-time blur and brightness are applied via a
// chained ID2D1Effect graph (CLSID_D2D1GaussianBlur -> a brightness
// transfer effect) so both operate on the actual rendered pixels every
// frame rather than a pre-baked approximation.
class BackgroundManager {
public:
    void Initialize(ID2D1DeviceContext* ctx, IWICImagingFactory* wic);

    // Applies new settings; triggers reload of image/slideshow/video
    // resources if the relevant paths/mode changed.
    void ApplySettings(const fcs::config::BackgroundSettings& settings);

    // Advances slideshow timers / gradient animation phase. Call once per
    // frame before Draw().
    void Update();

    // Draws the background to fill `viewport` (typically full screen).
    void Draw(const D2D1_RECT_F& viewport);

private:
    void ReloadImage();
    void ReloadSlideshow();
    void ReloadVideo();
    void RebuildEffectChain();

    void DrawBitmapCover(ID2D1Bitmap1* bitmap, const D2D1_RECT_F& viewport,
                          fcs::config::ScaleMode scaleMode, float opacity);
    void DrawSolid(const D2D1_RECT_F& viewport);
    void DrawGradient(const D2D1_RECT_F& viewport);
    void AdvanceSlideshow();

    ID2D1DeviceContext* m_ctx = nullptr;
    IWICImagingFactory* m_wic = nullptr;

    fcs::config::BackgroundSettings m_settings;
    bool m_needsImageReload = false;
    bool m_needsSlideshowReload = false;
    bool m_needsVideoReload = false;

    // Still image
    ComPtr<ID2D1Bitmap1> m_imageBitmap;

    // Slideshow
    std::vector<std::wstring> m_slideshowFiles;
    size_t m_slideshowIndex = 0;
    ComPtr<ID2D1Bitmap1> m_slideshowCurrent;
    ComPtr<ID2D1Bitmap1> m_slideshowNext;
    double m_slideshowElapsed = 0.0;
    bool m_slideshowCrossfading = false;
    double m_crossfadeElapsed = 0.0;
    std::mt19937 m_rng{std::random_device{}()};

    // Video
    std::unique_ptr<VideoBackground> m_video;

    // Effect graph: source bitmap -> Gaussian blur -> brightness (linear
    // transfer). Rebuilt whenever blur/brightness sliders change.
    ComPtr<ID2D1Effect> m_blurEffect;
    ComPtr<ID2D1Effect> m_brightnessEffect;
    int m_lastBlurAmount = -1;
    int m_lastBrightnessAmount = -1;

    fcs::animation::AnimationClock m_clock;
};

} // namespace fcs::background
