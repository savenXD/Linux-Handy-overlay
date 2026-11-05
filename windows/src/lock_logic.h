// Pure decision logic for the Alt+K lock and Alt+Shift+K kill switch (no Win32, unit-testable).
#pragma once

namespace cronq {
namespace lock_logic {

// KillAbort: another key was pressed while the kill switch was armed. The kill is cancelled and
// the key is passed on to the focused application unchanged (only Enter/Esc are swallowed).
enum class Action { None, Lock, Unlock, KillPrompt, KillConfirm, KillCancel, KillAbort };

struct State {
    bool locked = false;
    bool killPending = false;
};

// mods bits: 1 = Alt, 2 = Ctrl, 4 = Win, 8 = Shift (same packing as the keyboard hook).
// kHeld / xHeld: physical state of K and X *after* applying this event.
Action Evaluate(unsigned vk, bool down, bool wasDown, unsigned mods, bool kHeld, bool xHeld,
                const State& state);

// Kill confirmation timeout: shorter while the overlay is hidden (no visible prompt).
constexpr unsigned kKillTimeoutVisibleMs = 15000;
constexpr unsigned kKillTimeoutHiddenMs = 5000;

}  // namespace lock_logic
}  // namespace cronq
