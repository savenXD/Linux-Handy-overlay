#include <cstdio>

#include "lock_logic.h"

using namespace cronq::lock_logic;
#define CHECK(x)                                        \
    do {                                                \
        if (!(x)) {                                     \
            std::printf("FAIL %d: %s\n", __LINE__, #x); \
            return 1;                                   \
        }                                               \
    } while (0)

int main() {
    State open, locked, pending;
    locked.locked = true;
    pending.killPending = true;

    // Alt+K locks; plain K, Alt+Ctrl+K and key-up/repeat do not.
    CHECK(Evaluate('K', true, false, 1, true, false, open) == Action::Lock);
    CHECK(Evaluate('K', true, false, 0, true, false, open) == Action::None);
    CHECK(Evaluate('K', true, false, 3, true, false, open) == Action::None);
    CHECK(Evaluate('K', false, true, 1, false, false, open) == Action::None);
    CHECK(Evaluate('K', true, true, 1, true, false, open) == Action::None);

    // While locked, Alt+K again does nothing; only the Alt+K+X chord unlocks (either order).
    CHECK(Evaluate('K', true, false, 1, true, false, locked) == Action::None);
    CHECK(Evaluate('X', true, false, 1, false, true, locked) == Action::None);
    CHECK(Evaluate('X', true, false, 1, true, true, locked) == Action::Unlock);
    CHECK(Evaluate('K', true, false, 1, true, true, locked) == Action::Unlock);
    CHECK(Evaluate('X', true, false, 0, true, true, locked) == Action::None);  // Alt not held
    CHECK(Evaluate('G', true, false, 1, true, false, locked) == Action::None);

    // Alt+Shift+K opens the kill prompt in both states, but only once.
    CHECK(Evaluate('K', true, false, 9, true, false, open) == Action::KillPrompt);
    CHECK(Evaluate('K', true, false, 9, true, false, locked) == Action::KillPrompt);
    CHECK(Evaluate('K', true, false, 9, true, false, pending) == Action::None);

    // Enter confirms and Esc cancels only while the prompt is up.
    CHECK(Evaluate(0x0D, true, false, 0, false, false, pending) == Action::KillConfirm);
    CHECK(Evaluate(0x1B, true, false, 0, false, false, pending) == Action::KillCancel);
    CHECK(Evaluate(0x0D, true, false, 0, false, false, open) == Action::None);
    CHECK(Evaluate(0x0D, true, false, 0, false, false, locked) == Action::None);
    CHECK(Evaluate(0x0D, false, true, 0, false, false, pending) == Action::None);  // key-up
    CHECK(Evaluate(0x0D, true, true, 0, false, false, pending) == Action::None);   // held from before

    // Armed kill: any other non-modifier key aborts (and is passed on); modifiers and the chord do not.
    CHECK(Evaluate('A', true, false, 0, false, false, pending) == Action::KillAbort);
    CHECK(Evaluate(0x20, true, false, 0, false, false, pending) == Action::KillAbort);  // space
    CHECK(Evaluate('X', true, false, 1, false, true, pending) == Action::KillAbort);
    CHECK(Evaluate(0xA0, true, false, 8, false, false, pending) == Action::None);  // LSHIFT
    CHECK(Evaluate(0xA4, true, false, 1, false, false, pending) == Action::None);  // LMENU
    CHECK(Evaluate(0x14, true, false, 0, false, false, pending) == Action::None);  // CAPS LOCK
    CHECK(Evaluate('K', true, false, 9, true, false, pending) == Action::None);    // chord again
    CHECK(Evaluate('A', false, true, 0, false, false, pending) == Action::None);   // key-up
    CHECK(kKillTimeoutHiddenMs < kKillTimeoutVisibleMs);

    std::puts("lock_logic_test: all checks passed");
}
