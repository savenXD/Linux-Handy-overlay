#include "focus_guard.h"
#include "ghost.h"
namespace cronq {
namespace focus_guard {
namespace {
HWINEVENTHOOK g_hook = nullptr;
HWND g_overlay = nullptr, g_lastExternal = nullptr;
int64_t g_windowStart = 0;
int g_restores = 0;
bool Ours(HWND w) { return w && (w == g_overlay || IsChild(g_overlay, w)); }
void CALLBACK Foreground(HWINEVENTHOOK, DWORD, HWND hwnd, LONG, LONG, DWORD, DWORD) {
    if (!hwnd || !g_overlay) return;
    if (!Ours(hwnd)) {
        g_lastExternal = hwnd;
        ghost::OnForegroundChanged(hwnd);
        return;
    }
    int64_t now = NowUs();
    if (now - g_windowStart >= 1000000) {
        g_windowStart = now;
        g_restores = 0;
    }
    if (g_restores >= 10 || !IsWindow(g_lastExternal) || Ours(g_lastExternal)) return;
    ++g_restores;
    // The only focus operation targets the external window, never Cronq.
    if (SetForegroundWindow(g_lastExternal))
        LogErr("FOCUS_GUARD", "Overlay received focus; restored 0x%p", static_cast<void*>(g_lastExternal));
    else
        LogErr("FOCUS_GUARD", "Overlay received focus; restore to 0x%p was denied",
               static_cast<void*>(g_lastExternal));
}
}  // namespace
bool Init(HWND hwnd) {
    g_overlay = hwnd;
    HWND foreground = GetForegroundWindow();
    if (!Ours(foreground)) g_lastExternal = foreground;
    g_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, Foreground, 0, 0,
                             WINEVENT_OUTOFCONTEXT);
    if (!g_hook) LogErr("FOCUS_GUARD", "SetWinEventHook failed (%lu)", GetLastError());
    return g_hook != nullptr;
}
void Cleanup() {
    if (g_hook) UnhookWinEvent(g_hook);
    g_hook = nullptr;
    g_overlay = g_lastExternal = nullptr;
}
}  // namespace focus_guard
}  // namespace cronq
