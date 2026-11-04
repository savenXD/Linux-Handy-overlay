#include "input.h"

#include "hotkey.h"
#include "overlay.h"

namespace cronq {
namespace input {

namespace {

HWND g_targetHwnd = nullptr;
KeyMode g_keyMode = KeyMode::Mirror;
int64_t g_lastInputUs = 0;
TypingTarget g_target = TypingTarget::None;
GhostMode g_ghost = GhostMode::Off;
GrabStatus g_grabStatus = GrabStatus::None;
bool g_timerActive = false;

void StartIdleTimer() {
    if (g_targetHwnd) {
        g_lastInputUs = NowUs();
        g_timerActive = SetTimer(g_targetHwnd, kIdleTimerId, 60000, nullptr) != 0;
        if (!g_timerActive) {
            hotkey::SetTypingTarget(0);
            g_target = TypingTarget::None;
            LogErr("INPUT", "Idle timer failed; released input routing");
        }
    }
}

void StopIdleTimer() {
    if (g_targetHwnd && g_timerActive) {
        KillTimer(g_targetHwnd, kIdleTimerId);
        g_timerActive = false;
    }
}

}  // namespace

bool Init(HWND target) {
    g_targetHwnd = target;
    g_keyMode = KeyMode::Mirror;
    g_target = TypingTarget::None;
    g_ghost = GhostMode::Off;
    g_grabStatus = GrabStatus::None;
    hotkey::SetKeyMode(0);
    hotkey::SetTypingTarget(0);
    hotkey::SetClipboardShield(false);
    return true;
}

void Cleanup() {
    StopIdleTimer();
    SetTypingTarget(TypingTarget::None);
    g_targetHwnd = nullptr;
}

KeyMode GetKeyMode() { return g_keyMode; }

void SetKeyMode(KeyMode mode) {
    if (g_keyMode == mode) return;
    g_keyMode = mode;
    Log("INPUT", "Key mode set to: %s", mode == KeyMode::Exclusive ? "EXCLUSIVE" : "MIRROR");

    if (g_target == TypingTarget::Overlay) {
        if (mode == KeyMode::Exclusive) {
            hotkey::SetKeyMode(1);
            g_grabStatus = hotkey::IsHookInstalled() ? GrabStatus::Active : GrabStatus::Failed;
            StartIdleTimer();
        } else {
            hotkey::SetKeyMode(0);
            g_grabStatus = GrabStatus::None;
        }
    } else {
        hotkey::SetKeyMode(0);
        g_grabStatus = GrabStatus::None;
    }
    overlay::QueueDraw();
}

void CycleKeyMode() { SetKeyMode(g_keyMode == KeyMode::Mirror ? KeyMode::Exclusive : KeyMode::Mirror); }

TypingTarget GetTypingTarget() { return g_target; }

void SetTypingTarget(TypingTarget target) {
    if (g_target == target) {
        if (target == TypingTarget::Overlay) {
            StartIdleTimer();
        }
        return;
    }
    g_target = target;
    Log("INPUT", "Typing target set to: %s", target == TypingTarget::Overlay ? "OVERLAY" : "NONE");

    if (target == TypingTarget::Overlay) {
        const AppConfig& cfg = config::Get();
        if (cfg.shieldClipboardKeys) {
            hotkey::SetClipboardShield(true);
        }
        if (g_keyMode == KeyMode::Exclusive) {
            hotkey::SetKeyMode(1);
            g_grabStatus = hotkey::IsHookInstalled() ? GrabStatus::Active : GrabStatus::Failed;
        } else {
            hotkey::SetKeyMode(0);
            g_grabStatus = GrabStatus::None;
        }
        hotkey::SetTypingTarget(1);
        StartIdleTimer();
    } else {
        hotkey::SetClipboardShield(false);
        hotkey::SetKeyMode(0);
        hotkey::SetTypingTarget(0);
        g_grabStatus = GrabStatus::None;
        StopIdleTimer();
    }
    overlay::QueueDraw();
}

GrabStatus GetGrabStatus() {
    if (g_target == TypingTarget::Overlay && g_keyMode == KeyMode::Exclusive)
        return hotkey::IsHookInstalled() ? GrabStatus::Active : GrabStatus::Failed;
    return GrabStatus::None;
}

void ReleaseGrab() {
    if (g_grabStatus == GrabStatus::Active) {
        Log("INPUT", "Exclusive keyboard grab RELEASED.");
    }
    g_grabStatus = GrabStatus::None;
    hotkey::SetKeyMode(0);
    SetTypingTarget(TypingTarget::None);
}

GhostMode GetGhostMode() { return g_ghost; }

void SetGhostMode(GhostMode ghost) {
    if (g_ghost == ghost) return;
    g_ghost = ghost;
    if (ghost == GhostMode::On) {
        Log("INPUT", "Ghost mode ENABLED (transparent click-through).");
        if (g_target == TypingTarget::Overlay) {
            SetTypingTarget(TypingTarget::None);
        }
        overlay::SetGhost(true);
    } else {
        Log("INPUT", "Ghost mode DISABLED (solid overlay restored).");
        overlay::SetGhost(false);
    }
    overlay::QueueDraw();
}

void ToggleGhostMode() { SetGhostMode(g_ghost == GhostMode::Off ? GhostMode::On : GhostMode::Off); }

void OnMouseClick(bool inside) {
    if (inside) {
        if (g_ghost == GhostMode::On || !overlay::IsVisible()) {
            return;
        }
        SetTypingTarget(TypingTarget::Overlay);
    } else {
        SetTypingTarget(TypingTarget::None);
    }
}

void OnKeyActivity() {
    if (g_target == TypingTarget::Overlay) StartIdleTimer();
}

void OnIdleTimerExpired() {
    if (!g_timerActive || g_target != TypingTarget::Overlay || NowUs() - g_lastInputUs < 60000000) return;
    Log("INPUT", "60-second idle timeout expired. Releasing typing target and grabs.");
    SetTypingTarget(TypingTarget::None);
}

void NotifyOverlayHidden() { SetTypingTarget(TypingTarget::None); }

}  // namespace input
}  // namespace cronq
