#include "ImageLoader.h"

namespace fcs::background {

ComPtr<ID2D1Bitmap1> ImageLoader::LoadToBitmap(IWICImagingFactory* wic, ID2D1DeviceContext* ctx,
                                                const std::wstring& filePath) {
    if (!wic || !ctx || filePath.empty()) return nullptr;

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wic->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ,
                                                 WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return nullptr; // unsupported codec or missing file

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return nullptr;

    ComPtr<IWICFormatConverter> converter;
    hr = wic->CreateFormatConverter(&converter);
    if (FAILED(hr)) return nullptr;

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return nullptr;

    ComPtr<ID2D1Bitmap1> bitmap;
    hr = ctx->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bitmap);
    if (FAILED(hr)) return nullptr;

    return bitmap;
}

} // namespace fcs::background
