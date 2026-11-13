#include "wgc_capture.h"

#include <d3d11.h>
#include <d3d11_4.h>
#include <roapi.h>
#ifdef __MINGW32__
// mingw-w64's generated Foundation header specializes IReference twice for BYTE/boolean
// (both unsigned char). WGC uses neither boxed type; omit only the unused boolean specialization.
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#endif
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winstring.h>

#include <cwchar>

#include "capture_logic.h"
#include "com_ptr.h"
#include "image_logic.h"

namespace cronq {
namespace wgc_capture {
namespace {
namespace CaptureAbi = ABI::Windows::Graphics::Capture;
namespace Direct3DAbi = ABI::Windows::Graphics::DirectX::Direct3D11;
using Status = screen_capture::Status;

Status Failure(const char* operation, HRESULT hr, HRESULT* failure) {
    if (failure) *failure = hr;
    LogErr("SNAP", "WGC %s hr=0x%08lX", operation, static_cast<unsigned long>(hr));
    return hr == E_ACCESSDENIED ? Status::CaptureDenied : Status::Unavailable;
}

struct Runtime {
    HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
    ~Runtime() {
        if (SUCCEEDED(initialized)) RoUninitialize();
    }
};

template <typename T>
HRESULT Factory(const wchar_t* name, ComPtr<T>* result) {
    HSTRING className = nullptr;
    HRESULT hr = WindowsCreateString(name, static_cast<UINT32>(std::wcslen(name)), &className);
    if (SUCCEEDED(hr)) {
        hr = RoGetActivationFactory(className, __uuidof(T),
                                    reinterpret_cast<void**>(result->ReleaseAndGetAddressOf()));
        WindowsDeleteString(className);
    }
    return hr;
}

struct CloseObject {
    ComPtr<ABI::Windows::Foundation::IClosable> object;
    explicit CloseObject(IUnknown* value) {
        value->QueryInterface(__uuidof(ABI::Windows::Foundation::IClosable),
                              reinterpret_cast<void**>(object.ReleaseAndGetAddressOf()));
    }
    ~CloseObject() {
        if (object) {
            HRESULT hr = object->Close();
            if (FAILED(hr)) LogErr("SNAP", "WGC object close hr=0x%08lX", static_cast<unsigned long>(hr));
        }
    }
};

struct MappedTexture {
    ID3D11DeviceContext* context;
    ID3D11Texture2D* texture;
    ~MappedTexture() { context->Unmap(texture, 0); }
};

Status ReadFrame(CaptureAbi::IDirect3D11CaptureFrame* frame, ID3D11Device* device,
                 ID3D11DeviceContext* context, int width, int height, const std::atomic_bool& cancelled,
                 ULONGLONG deadline, std::vector<uint8_t>* pixels, HRESULT* failure) {
    auto fail = [&](const char* operation, HRESULT hr) { return Failure(operation, hr, failure); };
    ABI::Windows::Graphics::SizeInt32 content{};
    HRESULT hr = frame->get_ContentSize(&content);
    if (FAILED(hr)) return fail("content size", hr);
    if (content.Width != width || content.Height != height)
        return fail("display changed size during capture", E_UNEXPECTED);
    ComPtr<Direct3DAbi::IDirect3DSurface> surface;
    hr = frame->get_Surface(surface.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return fail("frame surface", hr);
    ComPtr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
    hr = surface.As(&access);
    if (FAILED(hr)) return fail("surface interop", hr);
    ComPtr<ID3D11Texture2D> texture, staging;
    hr = access->GetInterface(__uuidof(ID3D11Texture2D),
                              reinterpret_cast<void**>(texture.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) return fail("surface texture", hr);
    D3D11_TEXTURE2D_DESC source{};
    texture->GetDesc(&source);
    if (source.Format != DXGI_FORMAT_B8G8R8A8_UNORM || source.Width < UINT(width) ||
        source.Height < UINT(height) || source.SampleDesc.Count != 1)
        return fail("unexpected surface format or dimensions", E_UNEXPECTED);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = device->CreateTexture2D(&desc, nullptr, staging.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return fail("staging texture", hr);
    // The frame's content rectangle is valid; unused pool-texture pixels are not.
    D3D11_BOX box{0, 0, 0, UINT(width), UINT(height), 1};
    context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture.Get(), 0, &box);
    context->Flush();
    D3D11_MAPPED_SUBRESOURCE map{};
    do {
        if (cancelled) return Status::Cancelled;
        if (GetTickCount64() >= deadline) return fail("readback deadline", DXGI_ERROR_WAIT_TIMEOUT);
        hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &map);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING) Sleep(1);
    } while (hr == DXGI_ERROR_WAS_STILL_DRAWING);
    if (FAILED(hr)) return fail("map texture", hr);
    MappedTexture mapped{context, staging.Get()};
    // WGC supplies display-oriented frames, including portrait monitors; no DXGI rotation applies.
    return capture_logic::ReadSurface(static_cast<const uint8_t*>(map.pData), size_t(map.RowPitch) * height,
                                      width, height, map.RowPitch, capture_logic::Rotation::Identity, pixels)
               ? Status::Ok
               : Status::Unavailable;
}
ComPtr<IDXGIAdapter1> AdapterForMonitor(HMONITOR monitor) {
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                    reinterpret_cast<void**>(factory.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        LogErr("SNAP", "WGC adapter enumeration unavailable hr=0x%08lX; using default GPU",
               static_cast<unsigned long>(hr));
        return {};
    }
    for (UINT a = 0;; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(a, adapter.ReleaseAndGetAddressOf());
        if (FAILED(hr)) break;
        for (UINT o = 0;; ++o) {
            ComPtr<IDXGIOutput> output;
            if (FAILED(adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()))) break;
            DXGI_OUTPUT_DESC desc{};
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.AttachedToDesktop && desc.Monitor == monitor) {
                DXGI_ADAPTER_DESC1 gpu{};
                if (SUCCEEDED(adapter->GetDesc1(&gpu)))
                    Log("SNAP", "WGC target GPU vendor=0x%X device=0x%X LUID=%ld:%lu", gpu.VendorId,
                        gpu.DeviceId, gpu.AdapterLuid.HighPart, gpu.AdapterLuid.LowPart);
                return adapter;
            }
        }
    }
    Log("SNAP", "WGC target has no matching DXGI output; using default GPU");
    return {};
}

bool DeviceLost(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG ||
           hr == DXGI_ERROR_ACCESS_LOST;
}

Status CaptureOnce(HMONITOR monitor, HWND window, const RECT& bounds, const std::atomic_bool& cancelled,
                   ULONGLONG deadline, std::vector<uint8_t>* pixels, int* outputWidth, int* outputHeight,
                   HRESULT* failure) {
    auto fail = [&](const char* operation, HRESULT hr) { return Failure(operation, hr, failure); };
    if (cancelled) return Status::Cancelled;
    Runtime runtime;
    if (FAILED(runtime.initialized)) return fail("initialize WinRT", runtime.initialized);

    ComPtr<CaptureAbi::IGraphicsCaptureSessionStatics> support;
    HRESULT hr = Factory(L"Windows.Graphics.Capture.GraphicsCaptureSession", &support);
    if (FAILED(hr)) return fail("session factory (API unavailable)", hr);
    boolean supported = false;
    hr = support->IsSupported(&supported);
    if (FAILED(hr)) return fail("support check", hr);
    if (!supported) return fail("screen capture unsupported", E_NOTIMPL);
    ComPtr<IGraphicsCaptureItemInterop> interop;
    hr = Factory(L"Windows.Graphics.Capture.GraphicsCaptureItem", &interop);
    if (FAILED(hr)) return fail("item factory", hr);
    ComPtr<CaptureAbi::IGraphicsCaptureItem> item;
    if (window)
        hr = interop->CreateForWindow(window, __uuidof(CaptureAbi::IGraphicsCaptureItem),
                                      reinterpret_cast<void**>(item.ReleaseAndGetAddressOf()));
    else
        hr = interop->CreateForMonitor(monitor, __uuidof(CaptureAbi::IGraphicsCaptureItem),
                                       reinterpret_cast<void**>(item.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) return fail(window ? "create window item" : "create monitor item", hr);
    ABI::Windows::Graphics::SizeInt32 size{};
    hr = item->get_Size(&size);
    if (FAILED(hr)) return fail("item size", hr);
    size_t bytes = 0;
    if (!image_logic::PixelBytes(size.Width, size.Height, &bytes))
        return fail("invalid item size", E_INVALIDARG);
    if (!window && (size.Width != bounds.right - bounds.left || size.Height != bounds.bottom - bounds.top))
        return fail("monitor size changed", E_UNEXPECTED);
    int width = size.Width, height = size.Height;
    Log("SNAP", "WGC %s item size=%dx%d", window ? "window" : "monitor", width, height);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    auto adapter = AdapterForMonitor(monitor);
    hr = D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                           nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                           device.ReleaseAndGetAddressOf(), nullptr, context.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return fail("create D3D device", hr);
    // The free-threaded frame pool and our readback share this D3D device. Protect the
    // immediate context when WinRT delivers frames on its internal worker thread.
    ComPtr<ID3D11Multithread> multithread;
    hr = context.As(&multithread);
    if (FAILED(hr)) return fail("D3D multithread protection", hr);
    multithread->SetMultithreadProtected(TRUE);
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = device.As(&dxgiDevice);
    if (FAILED(hr)) return fail("DXGI device interop", hr);
    ComPtr<IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return fail("WinRT device interop", hr);
    ComPtr<Direct3DAbi::IDirect3DDevice> winrtDevice;
    hr = inspectable.As(&winrtDevice);
    if (FAILED(hr)) return fail("WinRT device", hr);
    ComPtr<CaptureAbi::IDirect3D11CaptureFramePoolStatics2> poolFactory;
    hr = Factory(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool", &poolFactory);
    if (FAILED(hr)) return fail("free-threaded frame pool factory", hr);
    ComPtr<CaptureAbi::IDirect3D11CaptureFramePool> pool;
    hr = poolFactory->CreateFreeThreaded(
        winrtDevice.Get(), ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized, 1,
        {width, height}, pool.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return fail("create frame pool", hr);
    CloseObject closePool(pool.Get());
    ComPtr<CaptureAbi::IGraphicsCaptureSession> session;
    hr = pool->CreateCaptureSession(item.Get(), session.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return fail("create session", hr);
    CloseObject closeSession(session.Get());
    ComPtr<CaptureAbi::IGraphicsCaptureSession2> session2;
    hr = session.As(&session2);
    if (FAILED(hr)) return fail("cursor control", hr);
    hr = session2->put_IsCursorCaptureEnabled(false);
    if (FAILED(hr)) return fail("disable cursor capture", hr);
    // Leave Windows' capture border enabled. Never request borderless access or suppress it.
    hr = session->StartCapture();
    if (FAILED(hr)) return fail("start capture", hr);
    ComPtr<CaptureAbi::IDirect3D11CaptureFrame> frame;
    while (!frame) {
        if (cancelled) return Status::Cancelled;
        if (GetTickCount64() >= deadline) return fail("frame deadline", DXGI_ERROR_WAIT_TIMEOUT);
        hr = pool->TryGetNextFrame(frame.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return fail("get frame", hr);
        if (!frame) Sleep(10);
    }
    CloseObject closeFrame(frame.Get());
    Status status = ReadFrame(frame.Get(), device.Get(), context.Get(), width, height, cancelled, deadline,
                              pixels, failure);
    if (status == Status::Ok) {
        *outputWidth = width;
        *outputHeight = height;
    } else {
        HRESULT removed = device->GetDeviceRemovedReason();
        if (FAILED(removed)) {
            LogErr("SNAP", "WGC device removed reason=0x%08lX", static_cast<unsigned long>(removed));
            if (DeviceLost(removed)) *failure = removed;
        }
    }
    return status;
}

Status CaptureWithRecovery(HMONITOR monitor, HWND window, const RECT& bounds,
                           const std::atomic_bool& cancelled, ULONGLONG deadline,
                           std::vector<uint8_t>* pixels, int* width, int* height) {
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        HRESULT failure = S_OK;
        auto status =
            CaptureOnce(monitor, window, bounds, cancelled, deadline, pixels, width, height, &failure);
        if (attempt == 0 && DeviceLost(failure) && !cancelled && GetTickCount64() < deadline) {
            Log("SNAP", "WGC device lost; recreating device, item, frame pool and session once");
            pixels->clear();
            continue;
        }
        return status;
    }
    return Status::Unavailable;
}
}  // namespace

Status CaptureMonitor(HMONITOR monitor, const RECT& bounds, const std::atomic_bool& cancelled,
                      ULONGLONG deadline, std::vector<uint8_t>* pixels) {
    int width = 0, height = 0;
    return CaptureWithRecovery(monitor, nullptr, bounds, cancelled, deadline, pixels, &width, &height);
}

screen_capture::Result CaptureWindow(HWND window, const std::atomic_bool& cancelled, ULONGLONG deadline) {
    screen_capture::Result result;
    RECT unused{};
    result.status = CaptureWithRecovery(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), window, unused,
                                        cancelled, deadline, &result.bgra, &result.width, &result.height);
    return result;
}
}  // namespace wgc_capture
}  // namespace cronq
