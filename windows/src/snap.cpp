#include "snap.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <fstream>
#include <memory>
#include <new>
#include <vector>

#include "com_ptr.h"
#include "config.h"
#include "image_codec.h"
#include "image_logic.h"
#include "overlay.h"
#include "screen_capture.h"
#include "snap_logic.h"
#include "ui_bridge.h"

namespace cronq {
namespace snap {

namespace {

bool g_isSnapping = false;
bool g_wasVisibleDuringSnap = false;
bool g_usedAffinity = false;
HANDLE g_worker = nullptr;
std::atomic_bool g_cancelled{false};
screen_capture::Backend g_backend = screen_capture::Backend::Automatic;
HWND g_windowTarget = nullptr;

// Capture, GPU readback, encoding and disk IO all run on the worker.
// Only overlay state and clipboard publication belong to the UI thread.
struct SnapJob {
    int width = 0, height = 0;
    std::vector<uint8_t> bgra, png;
    bool save = false, encoded = false, saved = false, latestUpdated = false;
    RECT bounds{};
    HWND foreground = nullptr;
    screen_capture::Status status = screen_capture::Status::Unavailable;
    int keepCount = 20;
    HWND notify = nullptr;
    screen_capture::Backend backend = screen_capture::Backend::Automatic;
};

UINT g_pngClipboardFormat = 0;
std::unique_ptr<SnapJob> g_pendingClipboard;
std::unique_ptr<SnapJob> g_workerJob;
unsigned g_clipboardAttempts = 0;

// Snaps live in %LOCALAPPDATA%\cronq\snaps: unlike Pictures, it is never synced
// to OneDrive.
std::wstring SnapDir(screen_capture::Backend backend) {
    std::wstring dir = DataDir() + (backend == screen_capture::Backend::WgcWindow ? L"\\snaps-wgc-window"
                                    : backend == screen_capture::Backend::Wgc     ? L"\\snaps-wgc"
                                                                                  : L"\\snaps");
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

enum class ClipboardResult { Published, Busy, Failed };
ClipboardResult CopyToClipboard(int width, int height, const uint8_t* bgra,
                                const std::vector<uint8_t>& pngData) {
    if (!OpenClipboard(overlay::Hwnd())) return ClipboardResult::Busy;
    // Prepare both formats before clearing the previous clipboard. Allocation
    // failure must not destroy a user's existing clipboard or result in an empty
    // clipboard marked as successful.
    size_t bytes = size_t(width) * height * 4;
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + bytes);
    if (dib) {
        auto* data = static_cast<uint8_t*>(GlobalLock(dib));
        if (data) {
            auto* header = reinterpret_cast<BITMAPINFOHEADER*>(data);
            *header = {};
            header->biSize = sizeof(BITMAPINFOHEADER);
            header->biWidth = width;
            header->biHeight = height;
            header->biPlanes = 1;
            header->biBitCount = 32;
            header->biCompression = BI_RGB;
            header->biSizeImage = static_cast<DWORD>(bytes);
            for (int y = 0; y < height; ++y)
                memcpy(data + sizeof(BITMAPINFOHEADER) + size_t(y) * width * 4,
                       bgra + size_t(height - 1 - y) * width * 4, size_t(width) * 4);
            GlobalUnlock(dib);
        } else {
            GlobalFree(dib);
            dib = nullptr;
        }
    }
    HGLOBAL png = nullptr;
    if (g_pngClipboardFormat && !pngData.empty()) {
        png = GlobalAlloc(GMEM_MOVEABLE, pngData.size());
        if (png) {
            void* data = GlobalLock(png);
            if (data) {
                memcpy(data, pngData.data(), pngData.size());
                GlobalUnlock(png);
            } else {
                GlobalFree(png);
                png = nullptr;
            }
        }
    }
    bool published = false;
    if ((dib || png) && EmptyClipboard()) {
        if (dib && SetClipboardData(CF_DIB, dib)) {
            published = true;
            dib = nullptr;
        }
        if (png && SetClipboardData(g_pngClipboardFormat, png)) {
            published = true;
            png = nullptr;
        }
    }
    if (dib) GlobalFree(dib);
    if (png) GlobalFree(png);
    CloseClipboard();
    return published ? ClipboardResult::Published : ClipboardResult::Failed;
}

void RestoreOverlay() {
    if (g_usedAffinity) {
        if (!SetWindowDisplayAffinity(overlay::Hwnd(), overlay::CaptureAffinity()))
            LogErr("SNAP", "Cannot restore capture affinity (%lu)", GetLastError());
    } else if (g_wasVisibleDuringSnap && overlay::IsVisible()) {
        SetWindowPos(overlay::Hwnd(), HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    g_usedAffinity = false;
    g_wasVisibleDuringSnap = false;
}

void Finish(const std::string& message, bool success) {
    Log(success ? "SNAP" : "SNAP-ERROR", "%s", message.c_str());
    overlay::ShowToast(message);
    ui::PostToUi(success ? "{\"type\":\"snap-state\",\"state\":\"done\"}"
                         : "{\"type\":\"snap-state\",\"state\":\"failed\"}");
    SetTimer(overlay::Hwnd(), kSnapRevertTimerId, 2500, nullptr);
    g_isSnapping = false;
}

void FinishJob(const SnapJob& job, const std::string& message, bool success) {
    Finish((job.backend == screen_capture::Backend::WgcWindow ? "WGC window test: "
            : job.backend == screen_capture::Backend::Wgc     ? "WGC test: "
                                                              : "") +
               message,
           success);
}

DWORD WINAPI CaptureWorker(LPVOID param) {
    auto* job = static_cast<SnapJob*>(param);
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        Log("SNAP", "%s capture requested",
            job->backend == screen_capture::Backend::WgcWindow ? "WGC window experimental"
            : job->backend == screen_capture::Backend::Wgc     ? "WGC experimental"
                                                               : "Automatic");
        auto capture = job->backend == screen_capture::Backend::WgcWindow
                           ? screen_capture::CaptureWindow(job->foreground, g_cancelled)
                           : screen_capture::Capture(job->bounds, job->foreground, g_cancelled, job->backend);
        job->status = capture.status;
        job->bgra = std::move(capture.bgra);
        if (job->status == screen_capture::Status::Ok) {
            job->width = capture.width;
            job->height = capture.height;
        }
        if (job->status == screen_capture::Status::Ok && !g_cancelled)
            job->encoded = image_codec::EncodePng(job->width, job->height, job->bgra.data(), &job->png);
        if (job->save && job->encoded && !g_cancelled) {
            std::wstring dir = SnapDir(job->backend);
            SYSTEMTIME st;
            GetLocalTime(&st);
            char stamp[64];
            snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay,
                     st.wHour, st.wMinute, st.wSecond);
            std::wstring filePath =
                dir + L"\\" + Widen(snap_logic::PickSnapName(ListSnapPngFiles(dir), stamp));
            std::wstring tempPath = filePath + L".tmp";
            std::ofstream f(tempPath.c_str(), std::ios::binary);
            if (f.is_open()) {
                f.write(reinterpret_cast<const char*>(job->png.data()),
                        static_cast<std::streamsize>(job->png.size()));
                f.close();
                if (!f) {
                    LogErr("SNAP", "Screenshot write failed");
                    DeleteFileW(tempPath.c_str());
                } else if (MoveFileExW(tempPath.c_str(), filePath.c_str(), MOVEFILE_WRITE_THROUGH)) {
                    job->saved = true;
                    Log("SNAP", "Saved screenshot");
                    std::wstring latestPath =
                        DataDir() + (job->backend == screen_capture::Backend::WgcWindow
                                         ? L"\\latest_snap_wgc_window.png"
                                     : job->backend == screen_capture::Backend::Wgc ? L"\\latest_snap_wgc.png"
                                                                                    : L"\\latest_snap.png");
                    std::wstring latestTemp = latestPath + L".tmp";
                    job->latestUpdated = CopyFileW(filePath.c_str(), latestTemp.c_str(), FALSE) &&
                                         MoveFileExW(latestTemp.c_str(), latestPath.c_str(),
                                                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
                    if (!job->latestUpdated) {
                        LogErr("SNAP", "Cannot update latest screenshot (%lu)", GetLastError());
                        DeleteFileW(latestTemp.c_str());
                    }
                } else {
                    LogErr("SNAP", "Cannot publish screenshot file (%lu)", GetLastError());
                    DeleteFileW(tempPath.c_str());
                }
            } else
                LogErr("SNAP", "Cannot open screenshot file");
            if (job->saved)
                for (const auto& oldName : snap_logic::FilesToPrune(ListSnapPngFiles(dir), job->keepCount))
                    DeleteFileW((dir + L"\\" + Widen(oldName)).c_str());
        }
    } catch (const std::exception&) {
        if (!job->encoded) job->status = screen_capture::Status::Unavailable;
        LogErr("SNAP", "Capture worker failed (allocation or IO error)");
    }
    if (g_cancelled) job->status = screen_capture::Status::Cancelled;
    if (SUCCEEDED(com)) CoUninitialize();
    if (!PostMessageW(job->notify, WM_CRONQ_SNAP_DONE, 0, reinterpret_cast<LPARAM>(job)))
        LogErr("SNAP", "Completion notification failed; worker poll will finish publication");
    return 0;
}

}  // namespace

// UI thread: restore the overlay, then publish a valid result with bounded
// clipboard retries.
void OnEncoded(void* param) {
    if (param != g_workerJob.get() || !g_workerJob) return;
    if (g_worker) {
        WaitForSingleObject(g_worker, INFINITE);
        CloseHandle(g_worker);
        g_worker = nullptr;
    }
    KillTimer(overlay::Hwnd(), kSnapWorkerTimerId);
    MSG message;
    while (PeekMessageW(&message, overlay::Hwnd(), WM_CRONQ_SNAP_DONE, WM_CRONQ_SNAP_DONE, PM_REMOVE)) {
    }
    auto job = std::move(g_workerJob);
    RestoreOverlay();
    if (job->status != screen_capture::Status::Ok) {
        FinishJob(*job, screen_capture::Message(job->status), false);
        return;
    }
    if (!job->encoded) {
        FinishJob(*job, "Screenshot encoding failed. Nothing copied or saved.", false);
        return;
    }
    g_pendingClipboard = std::move(job);
    g_clipboardAttempts = 0;
    OnClipboardRetry();
}

void OnWorkerPoll() {
    if (g_worker && WaitForSingleObject(g_worker, 0) == WAIT_OBJECT_0) OnEncoded(g_workerJob.get());
}

void OnClipboardRetry() {
    KillTimer(overlay::Hwnd(), kSnapClipboardTimerId);
    if (!g_pendingClipboard) return;
    auto& job = *g_pendingClipboard;
    ++g_clipboardAttempts;
    auto result = CopyToClipboard(job.width, job.height, job.bgra.data(), job.png);
    if (result == ClipboardResult::Busy && g_clipboardAttempts == 1)
        Log("SNAP", "Clipboard busy; retrying publication without blocking the UI");
    if (result == ClipboardResult::Busy && g_clipboardAttempts < 20 &&
        SetTimer(overlay::Hwnd(), kSnapClipboardTimerId, 25, nullptr))
        return;
    const bool copied = result == ClipboardResult::Published;
    if (!copied) LogErr("SNAP", "Clipboard publication failed after %u attempt(s)", g_clipboardAttempts);
    if (copied && (!job.save || (job.saved && job.latestUpdated)))
        FinishJob(job, job.save ? "Screenshot saved and copied." : "Screenshot copied.", true);
    else if (copied && job.saved)
        FinishJob(job,
                  "Screenshot saved and copied; latest screenshot index could not be "
                  "updated.",
                  false);
    else if (copied)
        FinishJob(job, "Screenshot copied, but could not be saved to disk.", false);
    else if (job.saved)
        FinishJob(job,
                  "Screenshot saved, but clipboard is unavailable. Open it from the "
                  "snaps folder.",
                  false);
    else
        FinishJob(job, "Screenshot could not be copied or saved.", false);
    g_pendingClipboard.reset();
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
        // The screen the user is working on: the foreground window's monitor
        // (primary if none).
        HWND active = GetForegroundWindow();
        POINT ptZero = {0, 0};
        HMONITOR hMon = active && active != overlay::Hwnd()
                            ? MonitorFromWindow(active, MONITOR_DEFAULTTOPRIMARY)
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
        RestoreOverlay();
        Finish("Screen dimensions exceed the screenshot memory limit.", false);
        return;
    }
    std::unique_ptr<SnapJob> job(new (std::nothrow) SnapJob());
    if (!job) {
        RestoreOverlay();
        Finish("Not enough memory to take a screenshot.", false);
        return;
    }
    job->width = rw;
    job->height = rh;
    job->bounds = {rx, ry, rx + rw, ry + rh};
    job->foreground =
        g_backend == screen_capture::Backend::WgcWindow ? g_windowTarget : GetForegroundWindow();
    if (job->foreground == overlay::Hwnd()) job->foreground = nullptr;
    job->save = cfg.snapSave;
    job->keepCount = cfg.snapKeepCount;
    job->notify = overlay::Hwnd();
    job->backend = g_backend;
    g_worker = CreateThread(nullptr, 0, CaptureWorker, job.get(), 0, nullptr);
    if (!g_worker) {
        RestoreOverlay();
        Finish("Could not start the screenshot worker. Try again.", false);
        return;
    }
    g_workerJob = std::move(job);  // UI owns the job until the worker is joined
    if (!SetTimer(overlay::Hwnd(), kSnapWorkerTimerId, 100, nullptr)) {
        Cleanup();
        Finish("Could not monitor the screenshot worker. Try again.", false);
    }
}

bool Init() {
    g_pngClipboardFormat = RegisterClipboardFormatW(L"PNG");
    g_isSnapping = false;
    return true;
}

void Cleanup() {
    g_cancelled = true;
    if (g_worker) {
        // The worker uses finite acquisition/readback waits and checks
        // cancellation. Join before destroying its HWND or shared state; never
        // leave a writer running after teardown.
        WaitForSingleObject(g_worker, INFINITE);
        CloseHandle(g_worker);
        g_worker = nullptr;
    }
    MSG message;
    while (PeekMessageW(&message, overlay::Hwnd(), WM_CRONQ_SNAP_DONE, WM_CRONQ_SNAP_DONE, PM_REMOVE)) {
    }
    g_workerJob.reset();
    g_pendingClipboard.reset();
    RestoreOverlay();
    if (overlay::Hwnd()) {
        KillTimer(overlay::Hwnd(), kSnapCaptureTimerId);
        KillTimer(overlay::Hwnd(), kSnapRevertTimerId);
        KillTimer(overlay::Hwnd(), kSnapClipboardTimerId);
        KillTimer(overlay::Hwnd(), kSnapWorkerTimerId);
    }
    g_isSnapping = false;
}

bool IsSnapping() { return g_isSnapping; }

static void StartCapture(screen_capture::Backend backend) {
    if (g_isSnapping) {
        Log("WINDOW", "overlay_snap: capture already in progress; debounced.");
        return;
    }
    g_isSnapping = true;
    g_backend = backend;
    g_windowTarget = backend == screen_capture::Backend::WgcWindow ? GetForegroundWindow() : nullptr;
    if (g_windowTarget) g_windowTarget = GetAncestor(g_windowTarget, GA_ROOT);
    DWORD targetPid = 0;
    if (g_windowTarget && GetWindowThreadProcessId(g_windowTarget, &targetPid) &&
        targetPid == GetCurrentProcessId())
        g_windowTarget = nullptr;  // do not accidentally capture Cronq's own UI
    g_cancelled = false;
    KillTimer(overlay::Hwnd(), kSnapRevertTimerId);
    ui::PostToUi("{\"type\":\"snap-state\",\"state\":\"snapping\"}");

    const AppConfig& cfg = config::Get();
    g_wasVisibleDuringSnap = overlay::IsVisible();

    if (g_wasVisibleDuringSnap) {
        // WDA_EXCLUDEFROMCAPTURE is supported by all Windows 11 releases.
        BOOL affinityOk = SetWindowDisplayAffinity(overlay::Hwnd(), 0x00000011);
        if (affinityOk) {
            g_usedAffinity = true;
        } else {
            g_usedAffinity = false;
            ShowWindow(overlay::Hwnd(), SW_HIDE);
        }
        int delay = std::max(60, cfg.snapDelayMs);
        if (!SetTimer(overlay::Hwnd(), kSnapCaptureTimerId, delay, nullptr)) {
            RestoreOverlay();
            Finish("Could not schedule the screenshot. Try again.", false);
        }
    } else {
        g_usedAffinity = false;
        if (!SetTimer(overlay::Hwnd(), kSnapCaptureTimerId, 1, nullptr)) {
            RestoreOverlay();
            Finish("Could not schedule the screenshot. Try again.", false);
        }
    }
}

void Run() { StartCapture(screen_capture::Backend::Automatic); }
void RunExperimental() { StartCapture(screen_capture::Backend::Wgc); }
void RunExperimentalWindow() { StartCapture(screen_capture::Backend::WgcWindow); }

}  // namespace snap
}  // namespace cronq
