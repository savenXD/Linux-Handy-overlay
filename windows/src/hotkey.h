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
    Mode,         // Alt+M
    GhostWrite,   // Alt+W
    Count
};

bool Init(HWND target);
void Cleanup();
void ReloadKeys();  // re-read the configurable letters from config.ini

void Dispatch(Id id);        // UI thread: dedupe window + action
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

}  // namespace hotkey
}  // namespace cronq
