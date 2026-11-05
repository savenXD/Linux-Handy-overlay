#include "killswitch.h"

#include <vector>

#include "hotkey.h"
#include "lock_logic.h"
#include "purge.h"
#include "overlay.h"

namespace cronq {
namespace killswitch {

namespace {

constexpr wchar_t kClassName[] = L"CronqKillPrompt";
constexpr UINT_PTR kTimeoutTimer = 1;

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
    // The red box is only drawn when the overlay is on screen. Pressed while hidden (or locked), the
    // prompt stays invisible but armed for 5 s: Enter confirms; Esc, any other key or the timeout cancels.
    const bool visible = overlay::IsVisible();
    SetTimer(g_prompt, kTimeoutTimer,
             visible ? lock_logic::kKillTimeoutVisibleMs : lock_logic::kKillTimeoutHiddenMs, nullptr);
    if (visible) {
        ShowWindow(g_prompt, SW_SHOWNOACTIVATE);
        SetWindowPos(g_prompt, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        UpdateWindow(g_prompt);
        Log("KILL", "Kill confirmation shown.");
    } else {
        Log("KILL", "Kill confirmation armed silently (overlay hidden).");
    }
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
    if (!purge::Launch()) {
        overlay::Show();
        overlay::ShowToast("Kill failed: could not start the cleanup");
        return;
    }
    HWND h = overlay::Hwnd();
    if (h) PostMessageW(h, WM_CRONQ_QUIT, 0, 0);
}

}  // namespace killswitch
}  // namespace cronq
