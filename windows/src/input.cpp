#include "input.h"

#include "hotkey.h"
#include "chat_view.h"
#include "overlay.h"

namespace cronq {
namespace input {

namespace {

TypingTarget g_target = TypingTarget::None;
GhostMode g_ghost = GhostMode::Off;
GrabStatus g_grabStatus = GrabStatus::None;
}  // namespace

bool Init(HWND) {
    g_target = TypingTarget::None;
    g_ghost = GhostMode::Off;
    g_grabStatus = GrabStatus::None;
    hotkey::SetTypingTarget(0);
    hotkey::SetClipboardShield(false);
    return true;
}

void Cleanup() {
    SetTypingTarget(TypingTarget::None);
}

void ToggleTypingTarget() {
    SetTypingTarget(g_target == TypingTarget::Overlay ? TypingTarget::None : TypingTarget::Overlay);
}

TypingTarget GetTypingTarget() { return g_target; }

void SetTypingTarget(TypingTarget target) {
    // A hidden overlay may retain routing, but cannot start a new capture.
    if (target == TypingTarget::Overlay &&
        ((!overlay::IsVisible() && g_target != TypingTarget::Overlay) || hotkey::IsLocked() || !hotkey::IsHookInstalled())) {
        g_grabStatus = hotkey::IsHookInstalled() ? GrabStatus::None : GrabStatus::Failed;
        target = TypingTarget::None;
    }
    if (g_target == target) {
        overlay::QueueDraw();
        return;
    }
    g_target = target;
    if (target == TypingTarget::Overlay) {
        hotkey::SetClipboardShield(config::Get().shieldClipboardKeys);
        chat_view::PrepareTypingTarget();
        hotkey::SetTypingTarget(1);
        g_grabStatus = GrabStatus::Active;
    } else {
        hotkey::SetTypingTarget(0);
        hotkey::SetClipboardShield(false);
        g_grabStatus = GrabStatus::None;
    }
    Log("INPUT", "Typing target: %s", g_target == TypingTarget::Overlay ? "OVERLAY" : "NORMAL");
    overlay::QueueDraw();
}

GrabStatus GetGrabStatus() {
    if (g_target == TypingTarget::Overlay)
        return hotkey::IsHookInstalled() ? GrabStatus::Active : GrabStatus::Failed;
    return g_grabStatus;
}

void ReleaseGrab() { SetTypingTarget(TypingTarget::None); }

GhostMode GetGhostMode() { return g_ghost; }

void SetGhostMode(GhostMode ghost) {
    if (g_ghost == ghost) return;
    g_ghost = ghost;
    if (ghost == GhostMode::On) {
        Log("INPUT", "Ghost mode ENABLED (transparent click-through).");
        if (g_target == TypingTarget::Overlay) {
            SetTypingTarget(TypingTarget::None);
        }
        overlay::SetOpacity(0.50);
        overlay::SetGhost(true);
    } else {
        Log("INPUT", "Ghost mode DISABLED (solid overlay restored).");
        overlay::SetOpacity(1.0);
        overlay::SetGhost(false);
    }
    overlay::QueueDraw();
}

void ToggleGhostMode() { SetGhostMode(g_ghost == GhostMode::Off ? GhostMode::On : GhostMode::Off); }

void OnMouseClick(bool inside) {
    // Only Alt+M or the typing indicator enables routing. Clicking normal UI controls
    // must not silently change where the next keystroke goes.
    if (!inside) ReleaseGrab();
}

}  // namespace input
}  // namespace cronq
