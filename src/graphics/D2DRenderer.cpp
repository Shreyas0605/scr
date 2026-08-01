#include "D2DRenderer.h"
#include <d3d11_4.h>
#include <dxgi1_3.h>

namespace fcs::graphics {

namespace {
void ThrowIfFailed(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char buf[256];
        sprintf_s(buf, "%s failed (hr=0x%08lX)", what, static_cast<unsigned long>(hr));
        throw std::runtime_error(buf);
    }
}
} // namespace

void D2DRenderer::Initialize(HWND hwnd, UINT clientWidth, UINT clientHeight, float dpi) {
    m_hwnd = hwnd;
    m_width = clientWidth > 0 ? clientWidth : 1;
    m_height = clientHeight > 0 ? clientHeight : 1;
    m_dpi = dpi > 0 ? dpi : 96.0f;

    CreateDeviceIndependentResources();
    CreateDeviceResources(hwnd);
}

void D2DRenderer::CreateDeviceIndependentResources() {
    D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    ThrowIfFailed(
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                           &options, reinterpret_cast<void**>(m_d2dFactory.GetAddressOf())),
        "D2D1CreateFactory");

    ThrowIfFailed(
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())),
        "DWriteCreateFactory");

    ThrowIfFailed(
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(m_wicFactory.GetAddressOf())),
        "CoCreateInstance(WICImagingFactory)");
}

void D2DRenderer::CreateDeviceResources(HWND hwnd) {
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };

    ComPtr<ID3D11Device> baseDevice;
    ComPtr<ID3D11DeviceContext> baseContext;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags, featureLevels,
        ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &baseDevice, nullptr, &baseContext);

    if (FAILED(hr)) {
        // Fall back to WARP (software) rendering so the screensaver still
        // runs on machines with broken/blocked GPU drivers.
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, creationFlags,
                                featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
                                &baseDevice, nullptr, &baseContext);
    }
    ThrowIfFailed(hr, "D3D11CreateDevice");

    ThrowIfFailed(baseDevice.As(&m_d3dDevice), "QI ID3D11Device");
    ThrowIfFailed(baseContext.As(&m_d3dContext), "QI ID3D11DeviceContext");

    // VideoBackground decodes on a worker thread and calls
    // ID2D1DeviceContext::CreateBitmap() from that thread to upload each
    // decoded frame. That's only safe if the underlying D3D11 device is
    // marked multithread-protected, which serializes concurrent access
    // from the decode thread and the render thread automatically.
    {
        ComPtr<ID3D11Multithread> mt;
        if (SUCCEEDED(m_d3dContext.As(&mt)) && mt) {
            mt->SetMultithreadProtected(TRUE);
        }
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ThrowIfFailed(m_d3dDevice.As(&dxgiDevice), "QI IDXGIDevice");

    ThrowIfFailed(m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice), "CreateDevice(D2D)");
    ThrowIfFailed(
        m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext),
        "CreateDeviceContext");

    ComPtr<IDXGIAdapter> adapter;
    ThrowIfFailed(dxgiDevice->GetAdapter(&adapter), "GetAdapter");
    ComPtr<IDXGIFactory2> dxgiFactory;
    ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)), "GetParent(IDXGIFactory2)");

    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width = m_width;
    scDesc.Height = m_height;
    scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scDesc.Stereo = FALSE;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = 2;
    scDesc.Scaling = DXGI_SCALING_NONE;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ThrowIfFailed(
        dxgiFactory->CreateSwapChainForHwnd(m_d3dDevice.Get(), hwnd, &scDesc, nullptr, nullptr,
                                             &m_swapChain),
        "CreateSwapChainForHwnd");
    // We handle Alt+Enter / DXGI window transitions ourselves (not relevant
    // for a fullscreen screensaver window, but disabling avoids surprises).
    dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

    CreateSwapChainBitmap();
    m_deviceLost = false;
}

void D2DRenderer::CreateSwapChainBitmap() {
    ComPtr<IDXGISurface> backBuffer;
    ThrowIfFailed(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "GetBuffer(0)");

    const D2D1_BITMAP_PROPERTIES1 bitmapProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), m_dpi, m_dpi);

    ThrowIfFailed(
        m_d2dContext->CreateBitmapFromDxgiSurface(backBuffer.Get(), &bitmapProps, &m_targetBitmap),
        "CreateBitmapFromDxgiSurface");

    m_d2dContext->SetTarget(m_targetBitmap.Get());
    m_d2dContext->SetDpi(m_dpi, m_dpi);
}

void D2DRenderer::Resize(UINT newWidth, UINT newHeight) {
    if (newWidth == 0 || newHeight == 0) return;
    if (newWidth == m_width && newHeight == m_height && m_targetBitmap) return;

    m_width = newWidth;
    m_height = newHeight;

    m_d2dContext->SetTarget(nullptr);
    m_targetBitmap.Reset();

    HRESULT hr = m_swapChain->ResizeBuffers(0, m_width, m_height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    if (FAILED(hr)) {
        m_deviceLost = true;
        return;
    }
    CreateSwapChainBitmap();
}

void D2DRenderer::SetDpi(float dpi) {
    if (dpi <= 0) return;
    m_dpi = dpi;
    if (m_d2dContext) m_d2dContext->SetDpi(m_dpi, m_dpi);
}

void D2DRenderer::BeginDraw() {
    m_d2dContext->BeginDraw();
}

bool D2DRenderer::EndDraw(bool vsync) {
    HRESULT hr = m_d2dContext->EndDraw();
    if (FAILED(hr)) {
        if (hr == D2DERR_RECREATE_TARGET) {
            m_deviceLost = true;
        }
        return false;
    }

    DXGI_PRESENT_PARAMETERS presentParams{};
    hr = m_swapChain->Present1(vsync ? 1 : 0, vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING, &presentParams);
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            m_deviceLost = true;
        }
        return false;
    }
    return true;
}

void D2DRenderer::Shutdown() {
    if (m_d2dContext) m_d2dContext->SetTarget(nullptr);
    m_targetBitmap.Reset();
    m_swapChain.Reset();
    m_d2dContext.Reset();
    m_d2dDevice.Reset();
    m_d3dContext.Reset();
    m_d3dDevice.Reset();
}

} // namespace fcs::graphics
