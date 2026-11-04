// Pure decision logic for the Alt+K lock and Alt+Shift+K kill switch (no Win32, unit-testable).
#pragma once

namespace cronq {
namespace lock_logic {

enum class Action { None, Lock, Unlock, KillPrompt, KillConfirm, KillCancel };

struct State {
    bool locked = false;
    bool killPending = false;
};

// mods bits: 1 = Alt, 2 = Ctrl, 4 = Win, 8 = Shift (same packing as the keyboard hook).
// kHeld / xHeld: physical state of K and X *after* applying this event.
Action Evaluate(unsigned vk, bool down, bool wasDown, unsigned mods, bool kHeld, bool xHeld,
                const State& state);

}  // namespace lock_logic
}  // namespace cronq
