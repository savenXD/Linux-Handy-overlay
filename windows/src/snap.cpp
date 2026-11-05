#include "snap.h"

#include <shlobj.h>
#include <wincodec.h>
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <memory>
#include <vector>

#include "com_ptr.h"
#include "config.h"
#include "overlay.h"
#include "snap_logic.h"
#include "image_codec.h"
#include "image_logic.h"
#include "ui_bridge.h"

namespace cronq {
namespace snap {

namespace {

bool g_isSnapping = false;
bool g_wasVisibleDuringSnap = false;
bool g_usedAffinity = false;
HANDLE g_worker = nullptr;

// Encoding a multi-monitor capture to PNG and writing it can take a second; it runs on a worker
// thread so the overlay never freezes. The UI thread then publishes the result to the clipboard.
struct SnapJob {
    int width = 0, height = 0;
    std::vector<uint8_t> bgra, png;
    bool save = false, encoded = false;
    int keepCount = 20;
    HWND notify = nullptr;
};

UINT g_pngClipboardFormat = 0;

// Snaps live in %LOCALAPPDATA%\cronq\snaps: unlike Pictures, it is never synced to OneDrive.
std::wstring SnapDir() {
    std::wstring dir = DataDir() + L"\\snaps";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::vector<std::string> ListSnapPngFiles(const std::wstring& dir) {
    std::vector<std::string> files;
    std::wstring pattern = dir + L"\\snap-*.png";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.push_back(Narrow(fd.cFileName));
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    return files;
}

void CopyToClipboard(int width, int height, const uint8_t* bgra, const std::vector<uint8_t>& pngData) {
    HWND target = overlay::Hwnd();
    if (!OpenClipboard(target)) {
        LogErr("SNAP", "OpenClipboard failed (%lu); screenshot remains saved on disk", GetLastError());
        return;
    }
    EmptyClipboard();

    // 1. CF_DIB (Bottom-up DIB)
    size_t dibPixelsSize = static_cast<size_t>(width) * height * 4;
    size_t totalDibSize = sizeof(BITMAPINFOHEADER) + dibPixelsSize;
    HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE, totalDibSize);
    if (hDib) {
        uint8_t* pDib = reinterpret_cast<uint8_t*>(GlobalLock(hDib));
        if (pDib) {
            BITMAPINFOHEADER* bih = reinterpret_cast<BITMAPINFOHEADER*>(pDib);
            ZeroMemory(bih, sizeof(BITMAPINFOHEADER));
            bih->biSize = sizeof(BITMAPINFOHEADER);
            bih->biWidth = width;
            bih->biHeight = height;  // positive = bottom-up
            bih->biPlanes = 1;
            bih->biBitCount = 32;
            bih->biCompression = BI_RGB;
            bih->biSizeImage = static_cast<DWORD>(dibPixelsSize);

            uint8_t* dst = pDib + sizeof(BITMAPINFOHEADER);
            // Flip top-down BGRA to bottom-up
            size_t stride = width * 4;
            for (int y = 0; y < height; ++y) {
                const uint8_t* srcRow = bgra + (height - 1 - y) * stride;
                uint8_t* dstRow = dst + y * stride;
                memcpy(dstRow, srcRow, stride);
            }
            GlobalUnlock(hDib);
            if (!SetClipboardData(CF_DIB, hDib)) GlobalFree(hDib);
        } else {
            GlobalFree(hDib);
        }
    }

    // 2. Registered "PNG" format
    if (!pngData.empty() && g_pngClipboardFormat != 0) {
        HGLOBAL hPng = GlobalAlloc(GMEM_MOVEABLE, pngData.size());
        if (hPng) {
            void* pPng = GlobalLock(hPng);
            if (pPng) {
                memcpy(pPng, pngData.data(), pngData.size());
                GlobalUnlock(hPng);
                if (!SetClipboardData(g_pngClipboardFormat, hPng)) GlobalFree(hPng);
            } else {
                GlobalFree(hPng);
            }
        }
    }

    CloseClipboard();
}

DWORD WINAPI EncodeWorker(LPVOID param) {
    auto* job = static_cast<SnapJob*>(param);
    const bool threaded = job->notify != nullptr;
    HRESULT com = threaded ? CoInitializeEx(nullptr, COINIT_MULTITHREADED) : S_FALSE;
    job->encoded = image_codec::EncodePng(job->width, job->height, job->bgra.data(), &job->png);
    if (job->save && job->encoded) {
        std::wstring dir = SnapDir();
        SYSTEMTIME st;
        GetLocalTime(&st);
        char stamp[64];
        snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour,
                 st.wMinute, st.wSecond);
        std::wstring filePath = dir + L"\\" + Widen(snap_logic::PickSnapName(ListSnapPngFiles(dir), stamp));
        std::ofstream f(filePath.c_str(), std::ios::binary);
        if (f.is_open()) {
            f.write(reinterpret_cast<const char*>(job->png.data()), static_cast<std::streamsize>(job->png.size()));
            f.close();
            if (!f) {
                LogErr("SNAP", "Screenshot write failed");
                DeleteFileW(filePath.c_str());
            } else {
                Log("SNAP", "Saved screenshot");
                std::wstring latestPath = DataDir() + L"\\latest_snap.png";
                if (!CopyFileW(filePath.c_str(), latestPath.c_str(), FALSE))
                    LogErr("SNAP", "Cannot update latest screenshot (%lu)", GetLastError());
            }
        }
        for (const auto& oldName : snap_logic::FilesToPrune(ListSnapPngFiles(dir), job->keepCount))
            DeleteFileW((dir + L"\\" + Widen(oldName)).c_str());
    }
    if (threaded) {
        if (SUCCEEDED(com)) CoUninitialize();
        if (!PostMessageW(job->notify, WM_CRONQ_SNAP_DONE, 0, reinterpret_cast<LPARAM>(job))) delete job;
    }
    return 0;
}

}  // namespace

// UI thread: the worker finished; publish to the clipboard and update the camera button.
void OnEncoded(void* param) {
    std::unique_ptr<SnapJob> job(static_cast<SnapJob*>(param));
    if (g_worker) {
        WaitForSingleObject(g_worker, INFINITE);  // it already posted: about to return
        CloseHandle(g_worker);
        g_worker = nullptr;
    }
    CopyToClipboard(job->width, job->height, job->bgra.data(), job->png);
    ui::PostToUi("{\"type\":\"snap-state\",\"state\":\"done\"}");
    if (job->encoded)
        SetTimer(overlay::Hwnd(), kSnapRevertTimerId, 1500, nullptr);
    else
        ui::PostToUi("{\"type\":\"snap-state\",\"state\":\"camera\"}");
    g_isSnapping = false;
}

void OnSnapRevert() {
    KillTimer(overlay::Hwnd(), kSnapRevertTimerId);
    ui::PostToUi("{\"type\":\"snap-state\",\"state\":\"camera\"}");
}

void OnSnapTimeout() {
    if (!g_isSnapping) return;
    KillTimer(overlay::Hwnd(), kSnapCaptureTimerId);
    const AppConfig& cfg = config::Get();

    int rx = 0, ry = 0, rw = 0, rh = 0;
    if (cfg.snapScope == "all") {
        rx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        ry = GetSystemMetrics(SM_YVIRTUALSCREEN);
        rw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        rh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    } else {
        // The screen the user is working on: the foreground window's monitor (primary if none).
        HWND active = GetForegroundWindow();
        POINT ptZero = {0, 0};
        HMONITOR hMon = active && active != overlay::Hwnd() ? MonitorFromWindow(active, MONITOR_DEFAULTTOPRIMARY)
                                                            : MonitorFromPoint(ptZero, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi;
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMon, &mi)) {
            rx = mi.rcMonitor.left;
            ry = mi.rcMonitor.top;
            rw = mi.rcMonitor.right - mi.rcMonitor.left;
            rh = mi.rcMonitor.bottom - mi.rcMonitor.top;
        } else {
            rw = GetSystemMetrics(SM_CXSCREEN);
            rh = GetSystemMetrics(SM_CYSCREEN);
        }
    }

    size_t checkedBytes = 0;
    if (!image_logic::PixelBytes(rw, rh, &checkedBytes)) {
        Cleanup();
        return;
    }
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bmp = CreateCompatibleBitmap(screenDc, rw, rh);
    HGDIOBJ oldBmp = memDc && bmp ? SelectObject(memDc, bmp) : nullptr;
    if (!screenDc || !memDc || !bmp || !oldBmp || oldBmp == HGDI_ERROR) {
        if (bmp) DeleteObject(bmp);
        if (memDc) DeleteDC(memDc);
        if (screenDc) ReleaseDC(nullptr, screenDc);
        LogErr("SNAP", "Cannot allocate capture bitmap (%lu)", GetLastError());
        Cleanup();
        ui::PostToUi("{\"type\":\"snap-state\",\"state\":\"camera\"}");
        return;
    }

    bool captured = screenDc && memDc && bmp && oldBmp &&
                    BitBlt(memDc, 0, 0, rw, rh, screenDc, rx, ry, SRCCOPY | CAPTUREBLT);

    SelectObject(memDc, oldBmp);
    ReleaseDC(nullptr, screenDc);
    DeleteDC(memDc);

    // Restore overlay visibility/affinity immediately
    if (g_usedAffinity) {
        SetWindowDisplayAffinity(overlay::Hwnd(), overlay::CaptureAffinity());
    } else if (g_wasVisibleDuringSnap && overlay::IsVisible()) {
        SetWindowPos(overlay::Hwnd(), HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    // Extract pixels into BGRA top-down buffer
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = rw;
    bi.bmiHeader.biHeight = -rh;  // negative = top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    size_t pixelBytes = static_cast<size_t>(rw) * rh * 4;
    std::vector<uint8_t> bgra(pixelBytes);
    HDC dc = GetDC(nullptr);
    captured = captured && GetDIBits(dc, bmp, 0, rh, bgra.data(), &bi, DIB_RGB_COLORS) == rh;
    ReleaseDC(nullptr, dc);
    DeleteObject(bmp);

    if (!captured) {
        LogErr("SNAP", "Screen capture failed (%lu)", GetLastError());
        Cleanup();
        ui::PostToUi("{\"type\":\"snap-state\",\"state\":\"camera\"}");
        return;
    }

    // Set alpha to 0xFF for all pixels
    for (size_t i = 3; i < pixelBytes; i += 4) {
        bgra[i] = 0xFF;
    }

    Log("WINDOW", "Captured snap: %dx%d at (%d, %d)", rw, rh, rx, ry);

    auto* job = new SnapJob();
    job->width = rw;
    job->height = rh;
    job->bgra = std::move(bgra);
    job->save = cfg.snapSave;
    job->keepCount = cfg.snapKeepCount;
    job->notify = overlay::Hwnd();
    g_worker = CreateThread(nullptr, 0, EncodeWorker, job, 0, nullptr);
    if (!g_worker) {  // no thread: do it here rather than lose the capture
        LogErr("SNAP", "Worker thread failed (%lu); encoding on the UI thread", GetLastError());
        job->notify = nullptr;
        EncodeWorker(job);
        OnEncoded(job);
    }
}

bool Init() {
    g_pngClipboardFormat = RegisterClipboardFormatW(L"PNG");
    g_isSnapping = false;
    return true;
}

void Cleanup() {
    if (g_worker) {  // let an in-flight encode finish writing; its result message is dropped
        WaitForSingleObject(g_worker, 5000);
        CloseHandle(g_worker);
        g_worker = nullptr;
    }
    if (g_usedAffinity)
        SetWindowDisplayAffinity(overlay::Hwnd(), overlay::CaptureAffinity());
    else if (g_isSnapping && g_wasVisibleDuringSnap && overlay::IsVisible())
        SetWindowPos(overlay::Hwnd(), HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_usedAffinity = false;
    if (overlay::Hwnd()) {
        KillTimer(overlay::Hwnd(), kSnapCaptureTimerId);
        KillTimer(overlay::Hwnd(), kSnapRevertTimerId);
    }
    g_isSnapping = false;
}

bool IsSnapping() { return g_isSnapping; }

void Run() {
    if (g_isSnapping) {
        Log("WINDOW", "overlay_snap: capture already in progress; debounced.");
        return;
    }
    g_isSnapping = true;
    ui::PostToUi("{\"type\":\"snap-state\",\"state\":\"snapping\"}");

    const AppConfig& cfg = config::Get();
    g_wasVisibleDuringSnap = overlay::IsVisible();

    if (g_wasVisibleDuringSnap) {
        // Try WDA_EXCLUDEFROMCAPTURE (0x00000011)
        BOOL affinityOk = SetWindowDisplayAffinity(overlay::Hwnd(), 0x00000011);
        if (affinityOk) {
            g_usedAffinity = true;
        } else {
            g_usedAffinity = false;
            ShowWindow(overlay::Hwnd(), SW_HIDE);
        }
        int delay = std::max(60, cfg.snapDelayMs);
        if (!SetTimer(overlay::Hwnd(), kSnapCaptureTimerId, delay, nullptr)) {
            Cleanup();
            OnSnapRevert();
        }
    } else {
        g_usedAffinity = false;
        if (!SetTimer(overlay::Hwnd(), kSnapCaptureTimerId, 1, nullptr)) {
            Cleanup();
            OnSnapRevert();
        }
    }
}

}  // namespace snap
}  // namespace cronq
