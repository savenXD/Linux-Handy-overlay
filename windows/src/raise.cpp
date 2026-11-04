#include "raise.h"

#include "overlay.h"

#include <dwmapi.h>

namespace cronq {
namespace raise {

namespace {

constexpr double kTokenRate = 300.0;     // max raises per second
constexpr double kTokenBurst = 60.0;     // max burst allowance
constexpr int64_t kCooldownUs = 50000;   // 50 ms cooldown when tripped
constexpr UINT kCooldownMs = 50;
constexpr UINT kWatchdogMs = 100;        // safety polling

bool g_active = false;
bool g_inCheck = false;
HWINEVENTHOOK g_hookForeground = nullptr;
HWINEVENTHOOK g_hookObjects = nullptr;
UINT_PTR g_watchdogTimer = 0;
UINT_PTR g_cooldownTimer = 0;

double g_tokens = kTokenBurst;
int64_t g_lastRefillUs = 0;
int64_t g_cooldownUntilUs = 0;
int64_t g_lastGuardLogUs = 0;
uint64_t g_raiseCount = 0;
uint64_t g_tripCount = 0;

bool ConsumeToken() {
    int64_t now = NowUs();
    if (now < g_cooldownUntilUs) return false;

    if (g_lastRefillUs == 0) {
        g_lastRefillUs = now;
        g_tokens = kTokenBurst;
    }
    double elapsed = (double)(now - g_lastRefillUs) / 1000000.0;
    g_tokens += elapsed * kTokenRate;
    if (g_tokens > kTokenBurst) g_tokens = kTokenBurst;
    g_lastRefillUs = now;

    if (g_tokens >= 1.0) {
        g_tokens -= 1.0;
        return true;
    }
    return false;
}

// Returns a visible window that sits above us and overlaps us, or nullptr.
HWND FindCoveringWindow(HWND self) {
    RECT mine;
    if (!GetWindowRect(self, &mine)) return nullptr;

    for (HWND w = GetWindow(self, GW_HWNDPREV); w; w = GetWindow(w, GW_HWNDPREV)) {
        if (!IsWindowVisible(w) || IsIconic(w)) continue;

        LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
        // Click-through layered windows are decorative HUDs (game overlays etc.); fighting them gains nothing.
        if ((ex & WS_EX_TRANSPARENT) && (ex & WS_EX_LAYERED)) continue;

        DWORD cloaked = 0;  // hidden UWP / virtual-desktop windows report visible but are cloaked
        if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;

        RECT r, inter;
        if (!GetWindowRect(w, &r)) continue;
        if (!IntersectRect(&inter, &mine, &r)) continue;
        return w;
    }
    return nullptr;
}

void CheckAndRaise();

void CALLBACK CooldownProc(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    g_cooldownTimer = 0;
    CheckAndRaise();
}

void CheckAndRaise() {
    if (!g_active || g_inCheck) return;
    HWND self = overlay::Hwnd();
    if (!self || !overlay::IsVisible()) return;

    g_inCheck = true;

    bool needRaise = false;
    HWND offender = nullptr;
    if (!(GetWindowLongPtrW(self, GWL_EXSTYLE) & WS_EX_TOPMOST)) {
        needRaise = true;  // someone stripped our TOPMOST flag
    } else {
        offender = FindCoveringWindow(self);
        needRaise = offender != nullptr;
    }

    if (needRaise) {
        if (ConsumeToken()) {
            SetWindowPos(self, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            g_raiseCount++;
        } else {
            int64_t now = NowUs();
            g_cooldownUntilUs = now + kCooldownUs;
            g_tripCount++;
            if (now - g_lastGuardLogUs > 1000000) {
                g_lastGuardLogUs = now;
                wchar_t cls[128] = L"";
                wchar_t title[128] = L"";
                if (offender) {
                    GetClassNameW(offender, cls, 128);
                    GetWindowTextW(offender, title, 128);
                }
                LogErr("LOOP_GUARD", "Raise limit exceeded (burst/rate saturated). Entering %u ms cooldown. Offender: class='%s' title='%s'",
                       (unsigned)kCooldownMs, Narrow(cls).c_str(), Narrow(title).c_str());
            }
            if (g_cooldownTimer == 0) {
                g_cooldownTimer = SetTimer(nullptr, 0, kCooldownMs, CooldownProc);
            }
        }
    }

    g_inCheck = false;
}

void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (!g_active) return;
    if (event != EVENT_SYSTEM_FOREGROUND) {
        // Object events: only whole-window changes matter.
        if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    }
    if (hwnd && hwnd == overlay::Hwnd()) return;
    CheckAndRaise();
}

void CALLBACK WatchdogProc(HWND, UINT, UINT_PTR, DWORD) { CheckAndRaise(); }

}  // namespace

bool Init() {
    g_tokens = kTokenBurst;
    g_lastRefillUs = 0;
    g_cooldownUntilUs = 0;
    Log("RAISE", "Restack watcher initialised (WinEvent hooks + %u ms watchdog).", (unsigned)kWatchdogMs);
    return true;
}

void Start() {
    if (g_active) return;
    g_active = true;

    g_hookForeground = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc, 0, 0,
                                       WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_hookObjects = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_REORDER, nullptr, WinEventProc, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!g_hookForeground || !g_hookObjects) {
        LogErr("RAISE", "SetWinEventHook failed (%lu); relying on the watchdog only.", GetLastError());
    }
    g_watchdogTimer = SetTimer(nullptr, 0, kWatchdogMs, WatchdogProc);
    Log("RAISE", "Restack watcher started.");
    CheckAndRaise();
}

void Stop() {
    if (!g_active) return;
    g_active = false;

    if (g_hookForeground) UnhookWinEvent(g_hookForeground);
    if (g_hookObjects) UnhookWinEvent(g_hookObjects);
    g_hookForeground = g_hookObjects = nullptr;
    if (g_watchdogTimer) KillTimer(nullptr, g_watchdogTimer);
    if (g_cooldownTimer) KillTimer(nullptr, g_cooldownTimer);
    g_watchdogTimer = g_cooldownTimer = 0;
    Log("RAISE", "Restack watcher stopped (raises=%llu, trips=%llu).", (unsigned long long)g_raiseCount,
        (unsigned long long)g_tripCount);
}

void Cleanup() { Stop(); }

uint64_t RaiseCount() { return g_raiseCount; }
uint64_t TripCount() { return g_tripCount; }

}  // namespace raise
}  // namespace cronq
