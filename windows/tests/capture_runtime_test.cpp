// Run on an unlocked Windows desktop. --require-dxgi disallows a skipped GPU
// test. Also runs under Wine for GDI, automatic fallback, blank rejection and
// cancellation.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#include "image_logic.h"
#include "ipc.h"
#include "screen_capture.h"
using namespace cronq;
using namespace cronq::screen_capture;
namespace {
bool black = false;
LRESULT CALLBACK Paint(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message != WM_PAINT) return DefWindowProcW(hwnd, message, wp, lp);
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT rect;
    GetClientRect(hwnd, &rect);
    bool background = GetWindowLongPtrW(hwnd, GWLP_USERDATA) == 1;
    HBRUSH brush = CreateSolidBrush(background ? RGB(0, 255, 0) : black ? RGB(0, 0, 0) : RGB(255, 0, 0));
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    if (!black && !background) {
        rect.left = (rect.right - rect.left) / 2;
        brush = CreateSolidBrush(RGB(0, 0, 255));
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
    EndPaint(hwnd, &paint);
    return 0;
}
void Pump() {
    auto end = GetTickCount64() + 150;
    do {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(2);
    } while (GetTickCount64() < end);
}
bool Pattern(const Result& result) {
    if (result.status != Status::Ok || result.width != 160 || result.height != 120 ||
        result.bgra.size() != 160 * 120 * 4)
        return false;
    const auto* red = result.bgra.data() + (60 * 160 + 40) * 4;
    const auto* blue = result.bgra.data() + (60 * 160 + 120) * 4;
    return red[2] > 240 && red[0] < 16 && red[1] < 16 && red[3] == 255 && blue[0] > 240 && blue[1] < 16 &&
           blue[2] < 16 && blue[3] == 255;
}
bool Green(const Result& result) {
    if (result.status != Status::Ok || result.bgra.size() != 160 * 120 * 4) return false;
    for (size_t i = 0; i < result.bgra.size(); i += 4)
        if (result.bgra[i] >= 16 || result.bgra[i + 1] <= 240 || result.bgra[i + 2] >= 16 ||
            result.bgra[i + 3] != 255)
            return false;
    return true;
}

bool HasFlag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], flag) == 0) return true;
    return false;
}
bool Sentinel(HWND hwnd) {
    if (!OpenClipboard(hwnd)) return false;
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, 4);
    bool ok = false;
    if (data) {
        auto* text = static_cast<wchar_t*>(GlobalLock(data));
        if (text) {
            text[0] = L'!';
            text[1] = 0;
            GlobalUnlock(data);
            ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data);
        }
        if (!ok) GlobalFree(data);
    }
    CloseClipboard();
    return ok;
}

bool Publication(HWND hwnd, const RECT& windowBounds) {
    if (!OpenClipboard(hwnd)) return false;
    int w = 0, h = 0;
    std::vector<uint8_t> pixels;
    HGLOBAL dib = GetClipboardData(CF_DIB);
    const auto* bytes = dib ? static_cast<const uint8_t*>(GlobalLock(dib)) : nullptr;
    bool ok = bytes && image_logic::DecodeDib(bytes, GlobalSize(dib), &w, &h, &pixels);
    if (bytes) GlobalUnlock(dib);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    ok = ok && GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor);
    int x = windowBounds.left - monitor.rcMonitor.left, y = windowBounds.top - monitor.rcMonitor.top;
    if (ok) {
        ok = x >= 0 && y >= 0 && x + 160 <= w && y + 120 <= h;
        if (ok) {
            const auto* red = pixels.data() + (size_t(y + 60) * w + x + 40) * 4;
            const auto* blue = pixels.data() + (size_t(y + 60) * w + x + 120) * 4;
            ok = red[2] > 240 && red[0] < 16 && blue[0] > 240 && blue[2] < 16;
        }
    }
    std::ifstream file((DataDir() + L"\\latest_snap.png").c_str(), std::ios::binary);
    std::vector<uint8_t> png((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    HGLOBAL pngHandle = GetClipboardData(RegisterClipboardFormatW(L"PNG"));
    const auto* clipboardPng = pngHandle ? static_cast<const uint8_t*>(GlobalLock(pngHandle)) : nullptr;
    ok = ok && png.size() > 8 && clipboardPng && GlobalSize(pngHandle) >= png.size() &&
         std::memcmp(png.data(), clipboardPng, png.size()) == 0;
    if (clipboardPng) GlobalUnlock(pngHandle);
    CloseClipboard();
    return ok;
}

struct ClipboardLock {
    HWND hwnd;
    HANDLE ready;
    DWORD duration;
    bool opened = false;
};
DWORD WINAPI HoldClipboard(void* param) {
    auto* lock = static_cast<ClipboardLock*>(param);
    lock->opened = OpenClipboard(lock->hwnd) != FALSE;
    WIN32_FILE_ATTRIBUTE_DATA before{}, after{};
    std::wstring path = DataDir() + L"\\latest_snap.png";
    GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &before);
    SetEvent(lock->ready);
    // Hold through the actual capture/encode/save, rather than guessing how long it takes.
    auto end = GetTickCount64() + 4000;
    while (GetTickCount64() < end) {
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &after) &&
            CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime) != 0)
            break;
        Sleep(10);
    }
    Sleep(lock->duration);
    if (lock->opened) CloseClipboard();
    return 0;
}
}  // namespace
int main(int argc, char** argv) {
    SetProcessDPIAware();
    WNDCLASSW wc{};
    wc.lpfnWndProc = Paint;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CronqCaptureTest";
    RegisterClassW(&wc);
    HWND background =
        CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, wc.lpszClassName, L"Capture test background",
                        WS_POPUP | WS_VISIBLE, 16, 16, 192, 152, nullptr, nullptr, wc.hInstance, nullptr);
    if (!background) return 1;
    SetWindowLongPtrW(background, GWLP_USERDATA, 1);
    InvalidateRect(background, nullptr, FALSE);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Capture test", WS_POPUP | WS_VISIBLE, 32,
                                32, 160, 120, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        DestroyWindow(background);
        return 1;
    }
    SetForegroundWindow(hwnd);
    Pump();
    if (argc > 1 && std::strcmp(argv[1], "--hold") == 0) {
        // Fixture for end-to-end daemon/clipboard checks on an otherwise empty test desktop.
        auto end = GetTickCount64() + 60000;
        while (GetTickCount64() < end && IsWindow(hwnd)) Pump();
        DestroyWindow(hwnd);
        DestroyWindow(background);
        return 0;
    }
    RECT bounds;
    GetWindowRect(hwnd, &bounds);
    std::atomic_bool cancelled{false};
    int failures = 0;
    auto check = [&](const char* name, bool ok) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    if (argc > 1 && std::strcmp(argv[1], "--daemon-snap") == 0) {
        auto snap = [&] {
            std::string reply;
            bool sent = ipc::Send("SNAP", &reply);
            auto end = GetTickCount64() + 5000;
            while (sent && !IsClipboardFormatAvailable(CF_DIB) && GetTickCount64() < end) Pump();
            return sent && Publication(hwnd, bounds);
        };
        check("prepare clipboard sentinel", Sentinel(hwnd));
        check("daemon publishes real pixels and identical disk/clipboard PNG", snap());
        check("reset clipboard sentinel", Sentinel(hwnd));
        ClipboardLock lock{hwnd, CreateEventW(nullptr, TRUE, FALSE, nullptr), 150};
        HANDLE thread = CreateThread(nullptr, 0, HoldClipboard, &lock, 0, nullptr);
        if (thread) WaitForSingleObject(lock.ready, 2000);
        check("clipboard contention fixture opened", thread && lock.opened);
        check("clipboard retry publishes the captured frame", snap());
        if (thread) {
            WaitForSingleObject(thread, INFINITE);
            CloseHandle(thread);
        }
        CloseHandle(lock.ready);
        DestroyWindow(hwnd);
        DestroyWindow(background);
        std::printf("capture_runtime_test daemon: %d failure(s)\n", failures);
        return failures ? 1 : 0;
    }
    check("GDI captures actual red/blue pixels", Pattern(Capture(bounds, hwnd, cancelled, Backend::Gdi)));
    check("automatic backend captures actual red/blue pixels", Pattern(Capture(bounds, hwnd, cancelled)));
    auto dxgi = Capture(bounds, hwnd, cancelled, Backend::Dxgi);
    if (dxgi.status == Status::Unavailable && !HasFlag(argc, argv, "--require-dxgi"))
        std::puts(
            "SKIP DXGI unavailable in this environment (native Windows GPU "
            "run still required)");
    else
        check("DXGI captures actual red/blue pixels", Pattern(dxgi));
    const bool testWgc = HasFlag(argc, argv, "--wgc") || HasFlag(argc, argv, "--require-wgc");
    bool wgcAvailable = false;
    if (testWgc) {
        auto wgc = Capture(bounds, hwnd, cancelled, Backend::Wgc);
        wgcAvailable = wgc.status == Status::Ok;
        if (wgc.status == Status::Unavailable && !HasFlag(argc, argv, "--require-wgc"))
            std::puts("SKIP WGC unavailable (run --require-wgc on native Windows 11)");
        else
            check("WGC captures actual red/blue pixels without fallback", Pattern(wgc));
    }
    const bool testWindow =
        HasFlag(argc, argv, "--wgc-window") || HasFlag(argc, argv, "--require-wgc-window");
    bool windowAvailable = false;
    if (testWindow) {
        auto windowCapture = CaptureWindow(hwnd, cancelled);
        windowAvailable = windowCapture.status == Status::Ok;
        if (windowCapture.status == Status::Unavailable && !HasFlag(argc, argv, "--require-wgc-window"))
            std::puts("SKIP WGC window capture unavailable (run --require-wgc-window on Windows 11)");
        else
            check("WGC captures the foreground window with correct dimensions", Pattern(windowCapture));
    }
    black = true;
    InvalidateRect(hwnd, nullptr, FALSE);
    Pump();
    auto blank = Capture(bounds, hwnd, cancelled);
    check("black result is rejected without publishing pixels",
          blank.status == Status::Blank && blank.bgra.empty());
    if (wgcAvailable) {
        auto blankWgc = Capture(bounds, hwnd, cancelled, Backend::Wgc);
        check("WGC black result rejected", blankWgc.status == Status::Blank && blankWgc.bgra.empty());
    }
    if (windowAvailable) {
        auto blankWindow = CaptureWindow(hwnd, cancelled);
        check("WGC black window rejected", blankWindow.status == Status::Blank && blankWindow.bgra.empty());
    }
    black = false;
    InvalidateRect(hwnd, nullptr, FALSE);
    Pump();
    if (SetWindowDisplayAffinity(hwnd, 1)) {
        Pump();
        auto protectedImage = Capture(bounds, hwnd, cancelled);
        check("explicit capture protection reported",
              protectedImage.status == Status::ProtectedContent && protectedImage.bgra.empty());
        auto raw = Capture(bounds, nullptr, cancelled, Backend::Gdi);
        check("GDI actually blanks WDA_MONITOR pixels", raw.status == Status::Blank && raw.bgra.empty());
        check("restore WDA_MONITOR protection", SetWindowDisplayAffinity(hwnd, 0) != FALSE);
        Pump();
        check("capture recovers after protection removed", Pattern(Capture(bounds, hwnd, cancelled)));
    } else if (HasFlag(argc, argv, "--require-protection"))
        check("WDA_MONITOR supported on native test machine", false);
    else
        std::puts("SKIP display affinity unsupported in this environment");
    if (SetWindowDisplayAffinity(hwnd, 0x11)) {
        Pump();
        auto excluded = Capture(bounds, hwnd, cancelled);
        check("excluded foreground window reported",
              excluded.status == Status::ProtectedContent && excluded.bgra.empty());
        check("GDI exclusion exposes green fixture behind window",
              Green(Capture(bounds, nullptr, cancelled, Backend::Gdi)));
        check("restore capture exclusion", SetWindowDisplayAffinity(hwnd, 0) != FALSE);
        Pump();
        check("capture recovers after exclusion removed", Pattern(Capture(bounds, hwnd, cancelled)));
    } else if (HasFlag(argc, argv, "--require-protection"))
        check("WDA_EXCLUDEFROMCAPTURE supported on native test machine", false);
    else
        std::puts("SKIP native capture exclusion unsupported in this environment");
    cancelled = true;
    auto stopped = Capture(bounds, hwnd, cancelled);
    check("cancellation returns no pixels", stopped.status == Status::Cancelled && stopped.bgra.empty());
    auto stoppedWindow = CaptureWindow(hwnd, cancelled);
    check("window cancellation returns no pixels",
          stoppedWindow.status == Status::Cancelled && stoppedWindow.bgra.empty());
    DestroyWindow(hwnd);
    DestroyWindow(background);
    std::printf("capture_runtime_test: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
