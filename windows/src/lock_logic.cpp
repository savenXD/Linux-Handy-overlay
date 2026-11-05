#include "lock_logic.h"

namespace cronq {
namespace lock_logic {

namespace {
constexpr unsigned kVkReturn = 0x0D;
constexpr unsigned kVkEscape = 0x1B;
constexpr unsigned kAlt = 1, kShift = 8;

bool IsModifier(unsigned vk) {
    return (vk >= 0x10 && vk <= 0x12) ||  // SHIFT CONTROL MENU
           (vk >= 0xA0 && vk <= 0xA5) ||  // L/R SHIFT CONTROL MENU
           vk == 0x5B || vk == 0x5C ||    // LWIN RWIN
           vk == 0x14 || vk == 0x90 || vk == 0x91;  // CAPITAL NUMLOCK SCROLL
}
}  // namespace

Action Evaluate(unsigned vk, bool down, bool wasDown, unsigned mods, bool kHeld, bool xHeld,
                const State& s) {
    if (!down || wasDown) return Action::None;  // key-up and auto-repeat never trigger anything

    // The confirmation owns Enter / Esc while it is armed; any other key cancels it, so ordinary
    // typing can never confirm a kill by accident.
    if (s.killPending) {
        if (vk == kVkReturn) return Action::KillConfirm;
        if (vk == kVkEscape) return Action::KillCancel;
        if (vk == 'K' && mods == (kAlt | kShift)) return Action::None;  // pressing the chord again
        if (!IsModifier(vk)) return Action::KillAbort;
        return Action::None;
    }

    // Full kill: Alt+Shift+K (works even while locked).
    if (vk == 'K' && mods == (kAlt | kShift)) return Action::KillPrompt;

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
