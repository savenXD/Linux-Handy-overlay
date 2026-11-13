// Global Alt+<key> hotkeys and low-level keyboard/mouse hooks on a dedicated thread.
#pragma once

#include "common.h"

namespace cronq {
namespace hotkey {

enum class Id : int {
    None = 0,
    Toggle,       // Alt+X
    Ghost,        // Alt+G
    OpacityDec,   // Alt+A
    OpacityInc,   // Alt+S
    Fullscreen,   // Alt+F
    DefaultSize,  // Alt+D
    Snap,         // Alt+P
    SnapWgc,      // Alt+O: temporary Windows Graphics Capture experiment
    SnapWgcWindow, // Alt+Shift+O: foreground window via WGC
    Mode,         // Alt+M
    GhostWrite,   // Alt+W
    Camouflage,   // Alt+C toggles minimal camouflage
    Tone,         // Alt+T switches Light/Dark without changing transparency
    DockLeft,     // Alt+Left Arrow: save bottom-left corner
    DockRight,    // Alt+Right Arrow: save bottom-right corner
    GodCopy,      // Ctrl+Alt+C reads selected text independently of Ctrl+C
    GodPaste,     // Ctrl+Alt+V sends the retained text
    Lock,         // Alt+K (hide + disable every other hotkey)
    Unlock,       // Alt+K+X chord while locked
    KillPrompt,   // Alt+Shift+K
    KillConfirm,  // Enter while the kill prompt is up
    KillCancel,   // Esc while the kill prompt is up
    Count
};

bool Init(HWND target);
void Cleanup();
void ReloadKeys();  // re-read the configurable letters from config.ini

void Dispatch(Id id, HWND source = nullptr);        // UI thread: dedupe window + action
void InjectAltSuppressor();  // UI thread: stops the focused app from opening its menu bar on Alt release

// Thread-safe state synchronisation for the hook worker thread
void UpdateOverlayRect(int left, int top, int right, int bottom);
void SetTypingTarget(int target);  // 0 = None, 1 = Overlay
void SetKeyMode(int mode);         // 0 = Mirror, 1 = Exclusive
void SetClipboardShield(bool enabled);
void SetTypingGuard(bool active);
bool IsHookInstalled();
bool CapsLockOn();
void Reinstall();
bool IsLocked();
void SetKillPending(bool pending);  // hook thread owns Enter/Esc while true
void SetOverlayVisible(bool visible);  // layout shortcuts pass through to apps while hidden

}  // namespace hotkey
}  // namespace cronq
