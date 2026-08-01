#pragma once
#include <d2d1_1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <string>

namespace fcs::background {

using Microsoft::WRL::ComPtr;

// Loads a still image (PNG/JPEG/BMP/GIF-first-frame/TIFF, plus WEBP and
// HEIF when the corresponding WIC codec is installed on the system - both
// ship as optional Microsoft Store codec packages on Windows 10/11) via
// WIC, converts to a premultiplied 32bpp BGRA buffer, and uploads it to a
// GPU-resident ID2D1Bitmap1 ready for drawing/effects.
class ImageLoader {
public:
    // Returns nullptr (does not throw) if the file can't be found or
    // decoded, e.g. an unsupported codec (missing WEBP/HEIF extension) or
    // a corrupt file -- callers should fall back to solid color in that
    // case rather than crash the screensaver.
    static ComPtr<ID2D1Bitmap1> LoadToBitmap(IWICImagingFactory* wic, ID2D1DeviceContext* ctx,
                                              const std::wstring& filePath);
};

} // namespace fcs::background
