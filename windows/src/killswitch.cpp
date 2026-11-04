#include "killswitch.h"

#include <vector>

#include "hotkey.h"
#include "overlay.h"

namespace cronq {
namespace killswitch {

namespace {

constexpr wchar_t kClassName[] = L"CronqKillPrompt";
constexpr UINT_PTR kTimeoutTimer = 1;
constexpr UINT kTimeoutMs = 15000;

HWND g_prompt = nullptr;
bool g_classRegistered = false;

int Scale(int v) {
    HDC dc = GetDC(nullptr);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return MulDiv(v, dpi, 96);
}

void DrawLine(HDC dc, const wchar_t* font, int px, int weight, COLORREF color, const std::wstring& text,
              RECT r, UINT flags) {
    HFONT f = CreateFontW(-Scale(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font);
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), (int)text.size(), &r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
    DeleteObject(f);
}

LRESULT CALLBACK PromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH bg = CreateSolidBrush(RGB(20, 22, 32));
            FillRect(dc, &rc, bg);
            DeleteObject(bg);
            HBRUSH border = CreateSolidBrush(RGB(214, 69, 80));
            FrameRect(dc, &rc, border);
            DeleteObject(border);
            SetBkMode(dc, TRANSPARENT);
            const int pad = Scale(22);
            RECT title = {pad, Scale(18), rc.right - pad, Scale(52)};
            DrawLine(dc, L"Segoe UI", 20, FW_BOLD, RGB(255, 107, 117), L"Kill Cronq permanently?", title,
                     DT_LEFT | DT_SINGLELINE);
            RECT body = {pad, Scale(60), rc.right - pad, Scale(140)};
            DrawLine(dc, L"Segoe UI", 14, FW_NORMAL, RGB(222, 226, 240),
                     L"This stops Cronq and deletes it from this PC: the program, settings, logins, "
                     L"screenshots and startup entries. It cannot be undone.",
                     body, DT_LEFT | DT_WORDBREAK);
            RECT foot = {pad, Scale(146), rc.right - pad, Scale(180)};
            DrawLine(dc, L"Segoe UI", 15, FW_SEMIBOLD, RGB(255, 255, 255),
                     L"Press ENTER to confirm    •    ESC to cancel", foot, DT_LEFT | DT_SINGLELINE);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_TIMER:
            if (wp == kTimeoutTimer) {
                Log("KILL", "Confirmation timed out; cancelled.");
                Dismiss();
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

std::wstring ExeDir() {
    wchar_t path[MAX_PATH * 2] = {};
    DWORD n = GetModuleFileNameW(nullptr, path, (DWORD)(sizeof(path) / sizeof(path[0])));
    std::wstring p(path, n);
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

bool LaunchCleanup() {
    const std::wstring installDir = ExeDir();
    const std::wstring script = installDir + L"\\killswitch.ps1";
    if (GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES) {
        LogErr("KILL", "killswitch.ps1 not found next to the executable; refusing to quit.");
        return false;
    }
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    // The script runs from %TEMP% because it deletes the install folder it came from.
    std::wstring copy = std::wstring(tmp) + L"cronq-kill-" + std::to_wstring(GetTickCount64()) + L".ps1";
    if (!CopyFileW(script.c_str(), copy.c_str(), FALSE)) {
        LogErr("KILL", "Could not copy cleanup script (%lu).", GetLastError());
        return false;
    }
    // Absolute path: never let a powershell.exe next to Cronq or in %TEMP% be picked up by the search order.
    wchar_t sys[MAX_PATH] = {};
    GetSystemDirectoryW(sys, MAX_PATH);
    const std::wstring ps = std::wstring(sys) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring cmd = L"\"" + ps + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" +
                       copy + L"\" -InstallDir \"" + installDir + L"\" -ParentPid " +
                       std::to_wstring(GetCurrentProcessId());
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    DWORD flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB;
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags, nullptr, tmp, &si, &pi);
    if (!ok) {  // the surrounding job may forbid breakaway
        buf.assign(cmd.begin(), cmd.end());
        buf.push_back(0);
        ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags & ~CREATE_BREAKAWAY_FROM_JOB,
                            nullptr, tmp, &si, &pi);
    }
    if (!ok) {
        LogErr("KILL", "Could not start cleanup script (%lu).", GetLastError());
        DeleteFileW(copy.c_str());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

}  // namespace

bool IsPending() { return g_prompt != nullptr; }

void ShowPrompt() {
    if (g_prompt) return;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!g_classRegistered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = PromptProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            LogErr("KILL", "RegisterClass failed (%lu).", GetLastError());
            return;
        }
        g_classRegistered = true;
    }
    const int w = Scale(440), h = Scale(196);
    RECT area = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    g_prompt = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"Cronq kill",
                               WS_POPUP, (area.right - w) / 2, (area.bottom - h) / 2, w, h, nullptr, nullptr,
                               inst, nullptr);
    if (!g_prompt) {
        LogErr("KILL", "Could not create the confirmation window (%lu).", GetLastError());
        hotkey::SetKillPending(false);  // the hook already claimed Enter/Esc; give them back
        return;
    }
    overlay::ProtectAllWindows();
    hotkey::SetKillPending(true);
    SetTimer(g_prompt, kTimeoutTimer, kTimeoutMs, nullptr);
    ShowWindow(g_prompt, SW_SHOWNOACTIVATE);
    SetWindowPos(g_prompt, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    UpdateWindow(g_prompt);
    Log("KILL", "Kill confirmation shown.");
}

void Dismiss() {
    hotkey::SetKillPending(false);
    if (!g_prompt) return;
    KillTimer(g_prompt, kTimeoutTimer);
    DestroyWindow(g_prompt);
    g_prompt = nullptr;
}

void Confirm() {
    if (!g_prompt) return;
    Dismiss();
    Log("KILL", "Kill confirmed; starting cleanup.");
    if (!LaunchCleanup()) {
        overlay::Show();
        overlay::ShowToast("Kill failed: cleanup script missing");
        return;
    }
    HWND h = overlay::Hwnd();
    if (h) PostMessageW(h, WM_CRONQ_QUIT, 0, 0);
}

}  // namespace killswitch
}  // namespace cronq
