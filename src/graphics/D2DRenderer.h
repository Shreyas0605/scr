#pragma once
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <string>
#include <stdexcept>

namespace fcs::graphics {

using Microsoft::WRL::ComPtr;

// Owns the hardware-accelerated rendering pipeline for one output window:
// a D3D11 device, a DXGI swap chain, a D2D1 device context bound to the
// swap chain's back buffer, and a shared DirectWrite factory. This is the
// single "GPU surface" every other subsystem (FlipClock, BackgroundManager)
// draws into.
//
// Rendering uses D2D1_DEVICE_CONTEXT (D2D 1.1+) over a DXGI swap chain
// rather than the legacy ID2D1HwndRenderTarget so that:
//   - Real-time Gaussian blur (ID2D1Effect / CLSID_D2D1GaussianBlur) works.
//   - Multi-monitor / mixed-DPI setups render bitmap resources 1:1.
//   - Present() uses DXGI_SWAP_EFFECT_FLIP_DISCARD for tear-free vsync.
class D2DRenderer {
public:
    D2DRenderer() = default;
    ~D2DRenderer() { Shutdown(); }

    D2DRenderer(const D2DRenderer&) = delete;
    D2DRenderer& operator=(const D2DRenderer&) = delete;

    // Creates the D3D11 device, DXGI swap chain for hwnd (sized to
    // clientWidth x clientHeight), the D2D1 device/context, and the shared
    // DirectWrite + WIC factories. Throws std::runtime_error on failure.
    void Initialize(HWND hwnd, UINT clientWidth, UINT clientHeight, float dpi);

    // Resizes the swap chain buffers and reacquires the D2D bitmap target.
    // Must be called on WM_SIZE / DPI change.
    void Resize(UINT newWidth, UINT newHeight);

    void SetDpi(float dpi);

    // BeginDraw/EndDraw bracket a single frame. EndDraw calls
    // IDXGISwapChain1::Present1 with sync interval 1 (vsync-locked) unless
    // vsync is disabled for preview windows.
    void BeginDraw();
    // Returns true on success; false (with device-lost flag set) if the
    // D3D device was lost and needs full reinitialization by the caller.
    bool EndDraw(bool vsync = true);

    bool IsDeviceLost() const { return m_deviceLost; }

    void Shutdown();

    ID2D1DeviceContext* Context() const { return m_d2dContext.Get(); }
    ID2D1Factory1* D2DFactory() const { return m_d2dFactory.Get(); }
    IDWriteFactory* DWriteFactory() const { return m_dwriteFactory.Get(); }
    IWICImagingFactory* WicFactory() const { return m_wicFactory.Get(); }
    ID3D11Device* D3DDevice() const { return m_d3dDevice.Get(); }

    UINT Width() const { return m_width; }
    UINT Height() const { return m_height; }
    float Dpi() const { return m_dpi; }

private:
    void CreateDeviceIndependentResources();
    void CreateDeviceResources(HWND hwnd);
    void CreateSwapChainBitmap();

    HWND m_hwnd = nullptr;
    UINT m_width = 0;
    UINT m_height = 0;
    float m_dpi = 96.0f;
    bool m_deviceLost = false;

    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<ID3D11DeviceContext> m_d3dContext;
    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID2D1Factory1> m_d2dFactory;
    ComPtr<ID2D1Device> m_d2dDevice;
    ComPtr<ID2D1DeviceContext> m_d2dContext;
    ComPtr<ID2D1Bitmap1> m_targetBitmap;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<IWICImagingFactory> m_wicFactory;
};

} // namespace fcs::graphics
