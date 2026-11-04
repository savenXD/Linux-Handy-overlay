#include "lock_logic.h"

namespace cronq {
namespace lock_logic {

namespace {
constexpr unsigned kVkReturn = 0x0D;
constexpr unsigned kVkEscape = 0x1B;
constexpr unsigned kAlt = 1, kShift = 8;
}  // namespace

Action Evaluate(unsigned vk, bool down, bool wasDown, unsigned mods, bool kHeld, bool xHeld,
                const State& s) {
    if (!down || wasDown) return Action::None;  // key-up and auto-repeat never trigger anything

    // The confirmation prompt owns Enter / Esc while it is on screen.
    if (s.killPending) {
        if (vk == kVkReturn) return Action::KillConfirm;
        if (vk == kVkEscape) return Action::KillCancel;
    }

    // Full kill: Alt+Shift+K (works even while locked).
    if (vk == 'K' && mods == (kAlt | kShift)) return s.killPending ? Action::None : Action::KillPrompt;

    if (!s.locked) {
        if (vk == 'K' && mods == kAlt) return Action::Lock;
        return Action::None;
    }

    // Locked: only the Alt+K+X chord (either press order) does anything.
    if (mods == kAlt && ((vk == 'K' && xHeld) || (vk == 'X' && kHeld))) return Action::Unlock;
    return Action::None;
}

}  // namespace lock_logic
}  // namespace cronq
