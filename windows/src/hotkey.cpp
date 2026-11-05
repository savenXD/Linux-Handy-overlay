#include "hotkey.h"

#include <cctype>
#include "config.h"
#include "ghost.h"
#include "input.h"
#include "input_logic.h"
#include "killswitch.h"
#include "lock_logic.h"
#include "overlay.h"
#include "snap.h"
#include "theme.h"

namespace cronq {
namespace hotkey {

namespace {

constexpr UINT kReinstallMessage = WM_APP + 41;
constexpr UINT kResyncMessage = WM_APP + 42;  // also forget all remembered key state
constexpr UINT kMouseHookMessage = WM_APP + 43;  // wParam 1 = install the mouse hook, 0 = remove it
constexpr int kCount = (int)Id::Count;

HWND g_target = nullptr;
HANDLE g_hookThread = nullptr;
DWORD g_hookThreadId = 0;
HANDLE g_hookReadyEvent = nullptr;

HHOOK g_kbdHook = nullptr;
HHOOK g_mouseHook = nullptr;
UINT_PTR g_reinstallTimer = 0;

DWORD g_vk[kCount] = {};
CRITICAL_SECTION g_vkCs;

input_logic::Router g_router;
volatile LONG g_installed = 0;
volatile LONG g_stopping = 0;
bool g_suppressorSent = false;
bool g_physicalDown[256] = {};
bool g_swallowedDown[256] = {};  // the hook swallowed this key's press, so Windows never saw it
volatile LONG g_capsLock = 0, g_numLock = 0;
int64_t g_lastTriggerUs[kCount] = {};

volatile LONG g_rectLeft = 0;
volatile LONG g_rectTop = 0;
volatile LONG g_rectRight = 0;
volatile LONG g_rectBottom = 0;

volatile LONG g_typingTarget = 0;   // 0 = None, 1 = Overlay
volatile LONG g_keyMode = 0;        // 0 = Mirror, 1 = Exclusive
volatile LONG g_shieldEnabled = 0;  // 1 = on
volatile LONG g_guardActive = 0;    // 1 = ghost writer typing guard
volatile LONG g_locked = 0;         // 1 = Alt+K lock: every hotkey except the unlock chord is off
volatile LONG g_killPending = 0;    // 1 = kill confirmation is armed
volatile LONG g_overlayVisible = 0; // 1 = overlay on screen; layout shortcuts only work then

int64_t DedupeUs(Id id) {
    switch (id) {
        case Id::Toggle:
            return 150000;
        case Id::Ghost:
            return 150000;
        case Id::OpacityDec:
            return 120000;
        case Id::OpacityInc:
            return 120000;
        case Id::Fullscreen:
            return 200000;
        case Id::DefaultSize:
            return 200000;
        case Id::Snap:
            return 200000;
        case Id::Mode:
            return 150000;
        case Id::GhostWrite:
            return 200000;
        case Id::Camouflage:
        case Id::Tone:
        case Id::DockLeft:
        case Id::DockRight:
            return 200000;
        default:
            return 0;
    }
}

const char* Describe(Id id) {
    switch (id) {
        case Id::Toggle:
            return "Alt+Toggle -> Toggling overlay";
        case Id::Ghost:
            return "Alt+Ghost -> Toggling ghost mode";
        case Id::OpacityDec:
            return "Alt+A -> Decreasing opacity";
        case Id::OpacityInc:
            return "Alt+S -> Increasing opacity";
        case Id::Fullscreen:
            return "Alt+F -> Fullscreen mode";
        case Id::DefaultSize:
            return "Alt+D -> Default size";
        case Id::Snap:
            return "Alt+Snap -> Taking snap";
        case Id::Mode:
            return "Alt+Mode -> Cycling key mode";
        case Id::GhostWrite:
            return "Alt+W -> Ghost Write";
        case Id::Camouflage:
            return "Alt+C -> Toggle camouflage";
        case Id::Tone:
            return "Alt+T -> Toggle Light/Dark";
        case Id::DockLeft:
            return "Alt+Left -> Bottom-left corner";
        case Id::DockRight:
            return "Alt+Right -> Bottom-right corner";
        case Id::Lock:
            return "Alt+K -> Locked (hidden, hotkeys disabled)";
        case Id::Unlock:
            return "Alt+K+X -> Unlocked";
        case Id::KillPrompt:
            return "Alt+Shift+K -> Kill confirmation";
        case Id::KillConfirm:
            return "Enter -> Kill confirmed";
        case Id::KillCancel:
            return "Esc -> Kill cancelled";
        default:
            return "?";
    }
}

DWORD VkFromChar(char c) {
    if (c >= 'a' && c <= 'z') return (DWORD)(c - 'a' + 'A');
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (DWORD)c;
    SHORT r = VkKeyScanW((wchar_t)(unsigned char)c);
    if (r == -1) return 0;
    return (DWORD)(r & 0xFF);
}

Id Match(DWORD vk) {
    if (vk == 0) return Id::None;
    EnterCriticalSection(&g_vkCs);
    Id matched = Id::None;
    for (int i = (int)Id::None + 1; i < kCount; i++) {
        if (g_vk[i] == vk) {
            matched = (Id)i;
            break;
        }
    }
    LeaveCriticalSection(&g_vkCs);
    return matched;
}

bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// Shortcuts that only change the overlay's window. While it is hidden they are not Cronq's, so
// the key reaches the focused application (Alt+Left stays "Back" in browsers, and so on).
bool IsLayoutHotkey(Id id) {
    switch (id) {
        case Id::Ghost: case Id::OpacityDec: case Id::OpacityInc: case Id::Fullscreen:
        case Id::DefaultSize: case Id::Mode: case Id::Camouflage: case Id::Tone:
        case Id::DockLeft: case Id::DockRight:
            return true;
        default:
            return false;
    }
}

// Start over from Windows' view of the keyboard (after unlock / resume, when releases were missed).
void ResyncKeyState() {
    for (unsigned vk = 0; vk < 256; ++vk) {
        g_physicalDown[vk] = Down(vk);
        g_swallowedDown[vk] = false;
    }
    g_physicalDown[VK_MENU] = g_physicalDown[VK_SHIFT] = g_physicalDown[VK_CONTROL] = false;
    g_router.Reset();
    g_suppressorSent = false;
}

unsigned Modifiers() {
    auto held = [](unsigned a, unsigned b, unsigned c) {
        return g_physicalDown[a] || g_physicalDown[b] || g_physicalDown[c];
    };
    return (held(VK_MENU, VK_LMENU, VK_RMENU) ? 1u : 0u) |
           (held(VK_CONTROL, VK_LCONTROL, VK_RCONTROL) ? 2u : 0u) |
           ((g_physicalDown[VK_LWIN] || g_physicalDown[VK_RWIN]) ? 4u : 0u) |
           (held(VK_SHIFT, VK_LSHIFT, VK_RSHIFT) ? 8u : 0u);
}

LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        if (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN) {
            const MSLLHOOKSTRUCT* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
            LONG l = InterlockedCompareExchange(&g_rectLeft, 0, 0);
            LONG t = InterlockedCompareExchange(&g_rectTop, 0, 0);
            LONG r = InterlockedCompareExchange(&g_rectRight, 0, 0);
            LONG b = InterlockedCompareExchange(&g_rectBottom, 0, 0);
            bool inside = (ms->pt.x >= l && ms->pt.x < r && ms->pt.y >= t && ms->pt.y < b);
            // The UI thread confirms "inside" with a real hit test (see app.cpp): in camouflage,
            // transparent pixels pass the click to the window underneath.
            PostMessageW(g_target, WM_CRONQ_CLICK, inside ? 1 : 0, MAKELPARAM(ms->pt.x, ms->pt.y));
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION) return CallNextHookEx(nullptr, code, wp, lp);
    const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
    if (kb->vkCode >= 256) return CallNextHookEx(nullptr, code, wp, lp);
    if (kb->dwExtraInfo == kInjectTag) {
        if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) {
            if (kb->vkCode == VK_CAPITAL) InterlockedXor(&g_capsLock, 1);
            if (kb->vkCode == VK_NUMLOCK) InterlockedXor(&g_numLock, 1);
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }
    const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
    const bool up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
    if (!down && !up) return CallNextHookEx(nullptr, code, wp, lp);
    DWORD vk = kb->vkCode;
    // Inside this hook Windows' async state still describes the moment *before* this event.
    const bool osHeld = Down(static_cast<int>(vk));
    // A modifier release that went to a secure desktop (Ctrl+Alt+Del, UAC) or an elevated window
    // never reaches this hook. Drop such stale "held" flags so plain letters are not hotkeys.
    if (down) input_logic::ClearStaleKeys(g_physicalDown, g_swallowedDown, vk, [](unsigned k) { return Down(static_cast<int>(k)); });
    const bool wasDown = g_physicalDown[vk] && (osHeld || g_swallowedDown[vk]);
    g_physicalDown[vk] = down;
    if (down && !wasDown && vk == VK_CAPITAL) InterlockedXor(&g_capsLock, 1);
    if (down && !wasDown && vk == VK_NUMLOCK) InterlockedXor(&g_numLock, 1);
    const unsigned mods = Modifiers();
    Id id = vk == 'C' ? Id::Camouflage : vk == 'T' ? Id::Tone :
            vk == VK_LEFT ? Id::DockLeft : vk == VK_RIGHT ? Id::DockRight : Match(vk);
    input_logic::Context context;
    if (IsLayoutHotkey(id) && !InterlockedCompareExchange(&g_overlayVisible, 0, 0)) id = Id::None;
    context.hotkey = id != Id::None && (mods == 1 || ((kb->flags & LLKHF_ALTDOWN) && !(mods & 14)));
    const bool locked = InterlockedCompareExchange(&g_locked, 0, 0) != 0;
    if (locked) {
        context.hotkey = false;  // locked: every normal hotkey is off
        id = Id::None;  // Swallowed chord keys must never dispatch their normal action.
    }
    lock_logic::State lockState;
    lockState.locked = locked;
    lockState.killPending = InterlockedCompareExchange(&g_killPending, 0, 0) != 0;
    const lock_logic::Action lockAction = lock_logic::Evaluate(
        vk, down, wasDown, mods, g_physicalDown['K'], g_physicalDown['X'], lockState);
    if (lockAction != lock_logic::Action::None) {
        switch (lockAction) {
            case lock_logic::Action::Lock:
                id = Id::Lock;
                InterlockedExchange(&g_locked, 1);
                break;
            case lock_logic::Action::Unlock:
                id = Id::Unlock;
                InterlockedExchange(&g_locked, 0);
                break;
            case lock_logic::Action::KillPrompt:
                id = Id::KillPrompt;
                InterlockedExchange(&g_killPending, 1);
                break;
            case lock_logic::Action::KillConfirm:
                id = Id::KillConfirm;
                InterlockedExchange(&g_killPending, 0);
                break;
            case lock_logic::Action::KillAbort:
                id = Id::None;  // the key goes on to the focused app; only the prompt is dismissed
                InterlockedExchange(&g_killPending, 0);
                PostMessageW(g_target, WM_CRONQ_HOTKEY, static_cast<WPARAM>(Id::KillCancel), 0);
                break;
            default:
                id = Id::KillCancel;
                InterlockedExchange(&g_killPending, 0);
                break;
        }
        // swallow the key (and its release) so no other app sees it
        context.hotkey = lockAction != lock_logic::Action::KillAbort;
    }
    else if (locked && mods == 1 && (vk == 'K' || vk == 'X')) {
        // Locked: swallow the chord keys (no action, id stays None) so the focused app never sees Alt+K / Alt+X.
        context.hotkey = true;
    }
    context.guard = InterlockedCompareExchange(&g_guardActive, 0, 0) != 0;
    context.target = InterlockedCompareExchange(&g_typingTarget, 0, 0) != 0;
    context.exclusive = InterlockedCompareExchange(&g_keyMode, 0, 0) != 0;
    context.shield = InterlockedCompareExchange(&g_shieldEnabled, 0, 0) != 0;
    context.ctrlOnly = mods == 2;
    context.osHeld = osHeld;
    const auto route = g_router.Key(vk, down, context);
    // Snapshot modifiers before the asynchronous UI handoff. Never query UI-thread keyboard state later.
    LPARAM packed = (down ? 1 : 0) | (static_cast<LPARAM>(mods) << 1) |
                    (InterlockedCompareExchange(&g_capsLock, 0, 0) ? (1 << 6) : 0) |
                    (InterlockedCompareExchange(&g_numLock, 0, 0) ? (1 << 7) : 0) |
                    (static_cast<LPARAM>(kb->scanCode & 255) << 16) |
                    ((kb->flags & LLKHF_EXTENDED) ? (1 << 24) : 0) | (route.repeat ? (1 << 25) : 0);
    if (up && (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU)) g_suppressorSent = false;
    if (route.hotkey && !((id==Id::Camouflage || id==Id::Tone || id==Id::DockLeft || id==Id::DockRight) && wasDown)) {
        PostMessageW(g_target, WM_CRONQ_HOTKEY, static_cast<WPARAM>(id), 0);
        if (!g_suppressorSent) {
            g_suppressorSent = true;
            PostMessageW(g_target, WM_CRONQ_ALTFIX, 0, 0);
        }
    }
    if (route.forward) PostMessageW(g_target, WM_CRONQ_KEY, vk, packed);
    if (route.clipboard) PostMessageW(g_target, WM_CRONQ_CLIPBOARD, vk, down ? 1 : 0);
    if (down) g_swallowedDown[vk] = route.swallow;
    if (up) g_swallowedDown[vk] = false;
    return route.swallow ? 1 : CallNextHookEx(nullptr, code, wp, lp);
}

// The low-level mouse hook runs for every mouse movement on the whole system (1000+ times a second
// with gaming mice) and adds latency to the cursor when Cronq is busy. It is only needed to notice
// clicks on or away from the overlay, so it exists only while the overlay is on screen.
void SetMouseHook(bool wanted) {
    if (wanted && !g_mouseHook && !InterlockedCompareExchange(&g_stopping, 0, 0)) {
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleW(nullptr), 0);
        if (!g_mouseHook) LogErr("HOTKEY", "SetWindowsHookEx(WH_MOUSE_LL) failed (%lu).", GetLastError());
    } else if (!wanted && g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
}

void InstallHooksInternal() {
    if (InterlockedCompareExchange(&g_stopping, 0, 0)) return;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    g_kbdHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, inst, 0);
    if (!g_kbdHook) {
        LogErr("HOTKEY", "SetWindowsHookEx(WH_KEYBOARD_LL) failed (%lu).", GetLastError());
    }
    SetMouseHook(InterlockedCompareExchange(&g_overlayVisible, 0, 0) != 0);
    InterlockedExchange(&g_installed, g_kbdHook ? 1 : 0);
}

void CALLBACK ReinstallProc(HWND, UINT, UINT_PTR, DWORD) {
    if (g_kbdHook) UnhookWindowsHookEx(g_kbdHook);
    if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
    g_kbdHook = nullptr;
    g_mouseHook = nullptr;
    InstallHooksInternal();
}

DWORD WINAPI HookThreadProc(LPVOID) {
    MSG queueMessage;
    PeekMessageW(&queueMessage, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    if (InterlockedCompareExchange(&g_stopping, 0, 0)) {
        SetEvent(g_hookReadyEvent);
        return 0;
    }
    ResyncKeyState();
    InterlockedExchange(&g_capsLock, GetKeyState(VK_CAPITAL) & 1);
    InterlockedExchange(&g_numLock, GetKeyState(VK_NUMLOCK) & 1);
    InstallHooksInternal();
    g_reinstallTimer = SetTimer(nullptr, 0, 15000, ReinstallProc);

    if (g_hookReadyEvent) {
        SetEvent(g_hookReadyEvent);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == kMouseHookMessage) {
            SetMouseHook(msg.wParam != 0);
            continue;
        }
        if (msg.message == kReinstallMessage || msg.message == kResyncMessage) {
            if (msg.message == kResyncMessage) ResyncKeyState();
            ReinstallProc(nullptr, 0, 0, 0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_reinstallTimer) {
        KillTimer(nullptr, g_reinstallTimer);
        g_reinstallTimer = 0;
    }
    if (g_kbdHook) {
        UnhookWindowsHookEx(g_kbdHook);
        g_kbdHook = nullptr;
    }
    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
    InterlockedExchange(&g_installed, 0);
    InterlockedExchange(&g_typingTarget, 0);
    InterlockedExchange(&g_keyMode, 0);
    return 0;
}

}  // namespace

void UpdateOverlayRect(int left, int top, int right, int bottom) {
    InterlockedExchange(&g_rectLeft, left);
    InterlockedExchange(&g_rectTop, top);
    InterlockedExchange(&g_rectRight, right);
    InterlockedExchange(&g_rectBottom, bottom);
}

void SetTypingTarget(int target) { InterlockedExchange(&g_typingTarget, target); }

void SetKeyMode(int mode) { InterlockedExchange(&g_keyMode, mode); }

void SetClipboardShield(bool enabled) { InterlockedExchange(&g_shieldEnabled, enabled ? 1 : 0); }

void SetTypingGuard(bool active) { InterlockedExchange(&g_guardActive, active ? 1 : 0); }

bool IsHookInstalled() { return InterlockedCompareExchange(&g_installed, 0, 0) != 0; }

bool IsLocked() { return InterlockedCompareExchange(&g_locked, 0, 0) != 0; }

void SetKillPending(bool pending) { InterlockedExchange(&g_killPending, pending ? 1 : 0); }

bool CapsLockOn() { return InterlockedCompareExchange(&g_capsLock, 0, 0) != 0; }

void Reinstall() {
    // Called after unlock / resume: key releases during the lock screen were never seen.
    if (g_hookThreadId) PostThreadMessageW(g_hookThreadId, kResyncMessage, 0, 0);
}

void SetOverlayVisible(bool visible) {
    if (InterlockedExchange(&g_overlayVisible, visible ? 1 : 0) == (visible ? 1 : 0)) return;
    if (g_hookThreadId) PostThreadMessageW(g_hookThreadId, kMouseHookMessage, visible ? 1 : 0, 0);
}

void ReloadKeys() {
    const AppConfig& cfg = config::Get();
    EnterCriticalSection(&g_vkCs);
    g_vk[(int)Id::Toggle] = VkFromChar(cfg.toggleKey ? cfg.toggleKey : 'x');
    g_vk[(int)Id::Ghost] = VkFromChar(cfg.ghostKey ? cfg.ghostKey : 'g');
    g_vk[(int)Id::OpacityDec] = 'A';
    g_vk[(int)Id::OpacityInc] = 'S';
    g_vk[(int)Id::Fullscreen] = 'F';
    g_vk[(int)Id::DefaultSize] = 'D';
    g_vk[(int)Id::Snap] = VkFromChar(cfg.snapKey ? cfg.snapKey : 'p');
    g_vk[(int)Id::Mode] = VkFromChar(cfg.modeKey ? cfg.modeKey : 'm');
    g_vk[(int)Id::GhostWrite] = VkFromChar(cfg.ghostWriterKey ? cfg.ghostWriterKey : 'w');
    LeaveCriticalSection(&g_vkCs);

    Log("HOTKEY",
        "Hotkeys: Alt+%c toggle, Alt+%c ghost, Alt+A/S opacity, Alt+F fullscreen, Alt+D default size, "
        "Alt+%c snap, Alt+%c mode, Alt+%c ghost writer.",
        (char)toupper(cfg.toggleKey), (char)toupper(cfg.ghostKey), (char)toupper(cfg.snapKey),
        (char)toupper(cfg.modeKey), (char)toupper(cfg.ghostWriterKey));
}

bool Init(HWND target) {
    g_target = target;
    InterlockedExchange(&g_stopping, 0);
    InitializeCriticalSection(&g_vkCs);
    ReloadKeys();

    g_hookReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_hookReadyEvent) {
        DeleteCriticalSection(&g_vkCs);
        return false;
    }
    g_hookThread = CreateThread(nullptr, 0, HookThreadProc, nullptr, 0, &g_hookThreadId);
    if (!g_hookThread) {
        LogErr("HOTKEY", "Failed to create hook worker thread (%lu).", GetLastError());
        CloseHandle(g_hookReadyEvent);
        g_hookReadyEvent = nullptr;
        DeleteCriticalSection(&g_vkCs);
        return false;
    }

    DWORD wait = WaitForSingleObject(g_hookReadyEvent, 2000);
    if (wait != WAIT_OBJECT_0 || !IsHookInstalled()) {
        LogErr("HOTKEY", "Hook worker thread initialization timed out or failed.");
        Cleanup();
        return false;
    }

    Log("HOTKEY", "Low-level keyboard and mouse hooks installed on dedicated thread.");
    return true;
}

void Cleanup() {
    InterlockedExchange(&g_stopping, 1);
    InterlockedExchange(&g_typingTarget, 0);
    InterlockedExchange(&g_keyMode, 0);
    if (g_hookThread) {
        PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
        if (WaitForSingleObject(g_hookThread, 2000) != WAIT_OBJECT_0) {
            LogErr("HOTKEY",
                   "Hook thread did not stop within 2s; retaining its synchronization objects until process "
                   "exit");
            return;
        }
        CloseHandle(g_hookThread);
        g_hookThread = nullptr;
        g_hookThreadId = 0;
    }
    if (g_hookReadyEvent) {
        CloseHandle(g_hookReadyEvent);
        g_hookReadyEvent = nullptr;
    }
    DeleteCriticalSection(&g_vkCs);
    Log("HOTKEY", "Hooks cleaned up.");
}

void InjectAltSuppressor() {
    if (!Down(VK_MENU)) return;
    INPUT in[2];
    ZeroMemory(in, sizeof(in));
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = 0xE8;  // unassigned dummy key
    in[0].ki.dwExtraInfo = kInjectTag;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

void Dispatch(Id id) {
    if (id <= Id::None || id >= Id::Count) return;
    // A normal shortcut may already be queued when the hook enters the locked state.
    if (IsLocked() && id >= Id::Toggle && id <= Id::DockRight) return;
    if (IsLayoutHotkey(id) && !overlay::IsVisible()) return;  // queued just before a hide
    int64_t now = NowUs();
    if (now - g_lastTriggerUs[(int)id] < DedupeUs(id)) {
        Log("HOTKEY", "Deduped %s (<%lld ms since last trigger)", Describe(id),
            (long long)(DedupeUs(id) / 1000));
        return;
    }
    g_lastTriggerUs[(int)id] = now;
    Log("HOTKEY", "%s", Describe(id));

    switch (id) {
        case Id::Toggle:
            input::ReleaseGrab();
            overlay::Toggle();
            break;
        case Id::Ghost:
            input::ToggleGhostMode();
            break;
        case Id::OpacityDec:
            overlay::SetOpacity(overlay::GetOpacity() - 0.10);
            break;
        case Id::OpacityInc:
            overlay::SetOpacity(overlay::GetOpacity() + 0.10);
            break;
        case Id::Fullscreen:
            overlay::SetFullscreen();
            break;
        case Id::DefaultSize:
            overlay::SetDefaultSize();
            break;
        case Id::Mode:
            input::ReleaseGrab();
            input::CycleKeyMode();
            break;
        case Id::Snap:
            snap::Run();
            break;
        case Id::GhostWrite:
            ghost::Run();
            break;
        case Id::Camouflage:
            if (!theme::ToggleCamouflage()) overlay::ShowToast("Could not save appearance");
            break;
        case Id::Tone:
            if (!theme::ToggleTone()) overlay::ShowToast("Could not save appearance");
            break;
        case Id::DockLeft:
        case Id::DockRight:
            overlay::DockBottom(id==Id::DockRight);
            break;
        case Id::Lock:
            ghost::Cancel();
            input::ReleaseGrab();
            overlay::Hide();
            break;
        case Id::Unlock:
            overlay::Show();
            overlay::ShowToast("Unlocked");
            break;
        case Id::KillPrompt:
            killswitch::ShowPrompt();
            break;
        case Id::KillConfirm:
            killswitch::Confirm();
            break;
        case Id::KillCancel:
            killswitch::Dismiss();
            break;
        default:
            break;
    }
}

}  // namespace hotkey
}  // namespace cronq
