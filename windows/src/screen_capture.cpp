#include "screen_capture.h"

#include <d3d11.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <cstring>

#include "capture_logic.h"
#include "com_ptr.h"
#include "image_logic.h"
#include "wgc_capture.h"

namespace cronq {
namespace screen_capture {
namespace {
using Status = screen_capture::Status;
struct Monitor {
    HMONITOR handle;
    RECT bounds;
};
BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT rect, LPARAM ctx) {
    try {
        reinterpret_cast<std::vector<Monitor>*>(ctx)->push_back({monitor, *rect});
    } catch (...) {
        return FALSE;  // never propagate an allocation exception through a Win32 callback
    }
    return TRUE;
}

bool SameInputDesktop() {
    HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!input) {
        LogErr("SNAP", "Cannot open input desktop (%lu)", GetLastError());
        return false;
    }
    wchar_t currentName[256]{}, inputName[256]{};
    DWORD needed = 0;
    bool same = GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, currentName,
                                          sizeof(currentName), &needed) &&
                GetUserObjectInformationW(input, UOI_NAME, inputName, sizeof(inputName), &needed) &&
                wcscmp(currentName, inputName) == 0;
    CloseDesktop(input);
    if (!same) LogErr("SNAP", "Input desktop differs or its identity could not be read");
    return same;
}

bool WindowProtectsCapture(HWND window) {
    if (!window) return false;
    DWORD pid = 0, affinity = 0;
    GetWindowThreadProcessId(window, &pid);
    SetLastError(ERROR_SUCCESS);
    if (!GetWindowDisplayAffinity(window, &affinity)) {
        Log("SNAP", "Target pid=%lu affinity=unknown (query error %lu)", pid, GetLastError());
        return false;  // a failed query does not establish either protection or permission
    }
    Log("SNAP", "Target pid=%lu affinity=0x%lX", pid, affinity);
    return affinity != 0;
}

Status DxgiError(const char* operation, HRESULT hr) {
    LogErr("SNAP", "DXGI %s hr=0x%08lX", operation, static_cast<unsigned long>(hr));
    if (hr == E_ACCESSDENIED || hr == DXGI_ERROR_SESSION_DISCONNECTED) return Status::DesktopUnavailable;
    return Status::Unavailable;
}

struct FrameLease {
    IDXGIOutputDuplication* duplication;
    ~FrameLease() { duplication->ReleaseFrame(); }
};

struct MappedTexture {
    ID3D11DeviceContext* context;
    ID3D11Texture2D* texture;
    ~MappedTexture() { context->Unmap(texture, 0); }
};

struct GdiResources {
    HDC screen = nullptr, memory = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    ~GdiResources() {
        if (old && old != HGDI_ERROR) SelectObject(memory, old);
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        if (screen) ReleaseDC(nullptr, screen);
    }
};

Status ReadOutput(IDXGIAdapter1* adapter, IDXGIOutput* output, const RECT& rect,
                  const std::atomic_bool& cancelled, ULONGLONG deadline, std::vector<uint8_t>* pixels) {
    size_t bytes = 0;
    if (!image_logic::PixelBytes(rect.right - rect.left, rect.bottom - rect.top, &bytes))
        return Status::Unavailable;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    HRESULT hr = D3D11CreateDevice(
        adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
        D3D11_SDK_VERSION, device.ReleaseAndGetAddressOf(), nullptr, context.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return DxgiError("create device", hr);
    ComPtr<IDXGIOutput1> output1;
    hr = output->QueryInterface(__uuidof(IDXGIOutput1),
                                reinterpret_cast<void**>(output1.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) return DxgiError("query output", hr);

    // Recreate once after access loss; the next shortcut always starts with fresh
    // devices/outputs.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (cancelled) return Status::Cancelled;
        ComPtr<IDXGIOutputDuplication> duplication;
        hr = output1->DuplicateOutput(device.Get(), duplication.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return DxgiError("duplicate output", hr);
        ComPtr<IDXGIResource> resource;
        DXGI_OUTDUPL_FRAME_INFO info{};
        do {
            if (cancelled) return Status::Cancelled;
            if (GetTickCount64() >= deadline) return DxgiError("frame deadline", DXGI_ERROR_WAIT_TIMEOUT);
            hr = duplication->AcquireNextFrame(50, &info, resource.ReleaseAndGetAddressOf());
        } while (hr == DXGI_ERROR_WAIT_TIMEOUT);
        if (hr == DXGI_ERROR_ACCESS_LOST && attempt == 0) continue;
        if (FAILED(hr)) return DxgiError("acquire frame", hr);
        FrameLease lease{duplication.Get()};
        if (info.ProtectedContentMaskedOut) return Status::ProtectedContent;

        ComPtr<ID3D11Texture2D> texture, staging;
        hr = resource.As(&texture);
        if (FAILED(hr)) return DxgiError("query texture", hr);
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        DXGI_OUTDUPL_DESC duplDesc{};
        duplication->GetDesc(&duplDesc);
        bool swapped = duplDesc.Rotation == DXGI_MODE_ROTATION_ROTATE90 ||
                       duplDesc.Rotation == DXGI_MODE_ROTATION_ROTATE270;
        if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
            (swapped ? desc.Height : desc.Width) != UINT(rect.right - rect.left) ||
            (swapped ? desc.Width : desc.Height) != UINT(rect.bottom - rect.top))
            return DxgiError("surface dimensions/format changed", E_UNEXPECTED);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        hr = device->CreateTexture2D(&desc, nullptr, staging.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return DxgiError("create staging texture", hr);
        context->CopyResource(staging.Get(), texture.Get());
        context->Flush();
        D3D11_MAPPED_SUBRESOURCE map{};
        do {
            if (cancelled) return Status::Cancelled;
            if (GetTickCount64() >= deadline) return DxgiError("readback deadline", DXGI_ERROR_WAIT_TIMEOUT);
            hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &map);
            if (hr == DXGI_ERROR_WAS_STILL_DRAWING) Sleep(1);
        } while (hr == DXGI_ERROR_WAS_STILL_DRAWING);
        if (FAILED(hr)) return DxgiError("map texture", hr);
        MappedTexture mapped{context.Get(), staging.Get()};
        auto rotation = capture_logic::Rotation::Identity;
        if (duplDesc.Rotation == DXGI_MODE_ROTATION_ROTATE90) rotation = capture_logic::Rotation::Rotate90;
        if (duplDesc.Rotation == DXGI_MODE_ROTATION_ROTATE180) rotation = capture_logic::Rotation::Rotate180;
        if (duplDesc.Rotation == DXGI_MODE_ROTATION_ROTATE270) rotation = capture_logic::Rotation::Rotate270;
        bool read = capture_logic::ReadSurface(static_cast<const uint8_t*>(map.pData),
                                               size_t(map.RowPitch) * desc.Height, desc.Width, desc.Height,
                                               map.RowPitch, rotation, pixels);
        return read ? Status::Ok : Status::Unavailable;
    }
    return Status::Unavailable;
}

Status CaptureDxgi(const Monitor& monitor, const std::atomic_bool& cancelled, ULONGLONG deadline,
                   std::vector<uint8_t>* pixels) {
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                    reinterpret_cast<void**>(factory.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) return DxgiError("create factory", hr);
    for (UINT a = 0;; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(a, adapter.ReleaseAndGetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) return DxgiError("enumerate adapters", hr);
        for (UINT o = 0;; ++o) {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf());
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(hr)) return DxgiError("enumerate outputs", hr);
            DXGI_OUTPUT_DESC desc{};
            hr = output->GetDesc(&desc);
            if (FAILED(hr)) return DxgiError("output description", hr);
            if (desc.AttachedToDesktop && desc.Monitor == monitor.handle)
                return ReadOutput(adapter.Get(), output.Get(), monitor.bounds, cancelled, deadline, pixels);
        }
    }
    return DxgiError("output not found", DXGI_ERROR_NOT_FOUND);
}

Status CaptureGdi(const RECT& rect, std::vector<uint8_t>* pixels) {
    int w = rect.right - rect.left, h = rect.bottom - rect.top;
    size_t bytes = 0;
    if (!image_logic::PixelBytes(w, h, &bytes)) return Status::Unavailable;
    GdiResources resources;
    auto& screen = resources.screen;
    auto& memory = resources.memory;
    auto& bitmap = resources.bitmap;
    auto& old = resources.old;
    screen = GetDC(nullptr);
    if (!screen) return Status::Unavailable;
    memory = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* data = nullptr;
    bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &data, nullptr, 0);
    old = memory && bitmap ? SelectObject(memory, bitmap) : nullptr;
    bool ok = old && old != HGDI_ERROR && data;
    if (ok) {
        std::memset(data, 0, bytes);
        ok = BitBlt(memory, 0, 0, w, h, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT) && GdiFlush();
        if (ok)
            ok = capture_logic::ReadSurface(static_cast<const uint8_t*>(data), bytes, w, h, size_t(w) * 4,
                                            capture_logic::Rotation::Identity, pixels);
    }
    if (!ok) LogErr("SNAP", "GDI capture failed (%lu)", GetLastError());
    return ok ? Status::Ok : Status::Unavailable;
}
}  // namespace

Result Capture(const RECT& bounds, HWND foreground, const std::atomic_bool& cancelled, Backend backend) {
    if (backend == Backend::WgcWindow) return CaptureWindow(foreground, cancelled);
    Result result;
    int w = bounds.right - bounds.left, h = bounds.bottom - bounds.top;
    size_t bytes = 0;
    if (!image_logic::PixelBytes(w, h, &bytes)) return result;
    if (cancelled) {
        result.status = Status::Cancelled;
        return result;
    }
    if (!SameInputDesktop()) {
        result.status = Status::DesktopUnavailable;
        return result;
    }
    if (WindowProtectsCapture(foreground)) {
        LogErr("SNAP", "Foreground window restricts capture");
        result.status = Status::ProtectedContent;
        return result;
    }
    std::vector<Monitor> monitors;
    if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors)))
        return result;
    result.bgra.resize(bytes, 0);
    for (size_t i = 3; i < bytes; i += 4) result.bgra[i] = 255;  // gaps between displays
    bool found = false;
    const ULONGLONG deadline = GetTickCount64() + (backend == Backend::Wgc ? 5000 : 2500);
    for (const auto& monitor : monitors) {
        RECT part{};
        if (!IntersectRect(&part, &bounds, &monitor.bounds)) continue;
        found = true;
        if (cancelled) {
            result.status = Status::Cancelled;
            result.bgra.clear();
            return result;
        }
        std::vector<uint8_t> output;
        auto status =
            backend == Backend::Wgc
                ? wgc_capture::CaptureMonitor(monitor.handle, monitor.bounds, cancelled,
                                              std::min(deadline, GetTickCount64() + 1500), &output)
            : backend == Backend::Gdi
                ? CaptureGdi(monitor.bounds, &output)
                : CaptureDxgi(monitor, cancelled, std::min(deadline, GetTickCount64() + 750), &output);
        const char* backendName = backend == Backend::Wgc ? "WGC" : backend == Backend::Gdi ? "GDI" : "DXGI";
        // Only compatibility failures or an ambiguous black frame permit fallback.
        // Explicit OS protection/access denial is reported, never turned into a
        // misleading 'successful' GDI snap.
        if (backend == Backend::Automatic &&
            (status == Status::Unavailable || (status == Status::Ok && capture_logic::IsBlack(output)))) {
            Log("SNAP", "DXGI unavailable or black; trying GDI compatibility capture");
            if (!SameInputDesktop())
                status = Status::DesktopUnavailable;
            else if (cancelled)
                status = Status::Cancelled;
            else {
                output.clear();
                status = CaptureGdi(monitor.bounds, &output);
                backendName = "GDI";
            }
        }
        if (status != Status::Ok) {
            result.status = status;
            result.bgra.clear();
            return result;
        }
        Log("SNAP", "%s captured output %ldx%ld at (%ld,%ld)", backendName,
            monitor.bounds.right - monitor.bounds.left, monitor.bounds.bottom - monitor.bounds.top,
            monitor.bounds.left, monitor.bounds.top);
        size_t stride = size_t(monitor.bounds.right - monitor.bounds.left) * 4;
        for (LONG y = part.top; y < part.bottom; ++y) {
            const uint8_t* src = output.data() + size_t(y - monitor.bounds.top) * stride +
                                 size_t(part.left - monitor.bounds.left) * 4;
            uint8_t* dst = result.bgra.data() + (size_t(y - bounds.top) * w + part.left - bounds.left) * 4;
            std::memcpy(dst, src, size_t(part.right - part.left) * 4);
        }
    }
    if (cancelled)
        result.status = Status::Cancelled;
    else if (!SameInputDesktop())
        result.status = Status::DesktopUnavailable;
    else if (!found)
        result.status = Status::Unavailable;
    else
        result.status = capture_logic::IsBlack(result.bgra) ? Status::Blank : Status::Ok;
    if (result.status != Status::Ok)
        result.bgra.clear();
    else {
        result.width = w;
        result.height = h;
    }
    return result;
}

Result CaptureWindow(HWND window, const std::atomic_bool& cancelled) {
    Result result;
    if (cancelled) {
        result.status = Status::Cancelled;
        return result;
    }
    if (!SameInputDesktop()) {
        result.status = Status::DesktopUnavailable;
        return result;
    }
    DWORD pid = 0;
    if (!window || !IsWindow(window) || !IsWindowVisible(window) || IsIconic(window) ||
        !GetWindowThreadProcessId(window, &pid)) {
        LogErr("SNAP", "WGC window target is unavailable or minimized");
        return result;
    }
    if (WindowProtectsCapture(window)) {
        result.status = Status::ProtectedContent;
        return result;
    }
    Log("SNAP", "WGC direct-window capture requested (pid=%lu)", pid);
    result = wgc_capture::CaptureWindow(window, cancelled, GetTickCount64() + 5000);
    if (cancelled)
        result.status = Status::Cancelled;
    else if (!SameInputDesktop())
        result.status = Status::DesktopUnavailable;
    else if (result.status == Status::Ok && capture_logic::IsBlack(result.bgra))
        result.status = Status::Blank;
    if (result.status != Status::Ok) {
        result.bgra.clear();
        result.width = result.height = 0;
    }
    return result;
}

const char* Message(Status status) {
    switch (status) {
        case Status::Blank:
            return "Screenshot was entirely black. Content may be protected or the "
                   "capture driver unavailable.";
        case Status::ProtectedContent:
            return "Windows or the active app is protecting screen content. Screenshot "
                   "not saved.";
        case Status::DesktopUnavailable:
            return "Cannot capture this desktop. Return to your normal unlocked "
                   "desktop.";
        case Status::Cancelled:
            return "Screenshot cancelled.";
        case Status::CaptureDenied:
            return "Windows denied access to this capture method. See cronq.log for details.";
        case Status::Unavailable:
            return "Screen capture failed. See cronq.log for capture diagnostics.";
        case Status::Ok:
            return "Screenshot captured.";
    }
    return "Screen capture failed.";
}
}  // namespace screen_capture
}  // namespace cronq
