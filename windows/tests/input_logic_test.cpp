#include "input_logic.h"
#include <cstdio>
using namespace cronq::input_logic;
#define CHECK(x)                                        \
    do {                                                \
        if (!(x)) {                                     \
            std::printf("FAIL %d: %s\n", __LINE__, #x); \
            return 1;                                   \
        }                                               \
    } while (0)
int main() {
    Router r;
    Context c;
    c.target = true;
    CHECK(r.Key('A', true, c).forward);
    CHECK(r.Key('A', false, c).forward);  // Mirror releases must reach WebView2.
    c.exclusive = true;
    CHECK(r.Key('B', true, c).swallow);
    auto repeat = r.Key('B', true, c);
    CHECK(repeat.forward && repeat.repeat && repeat.swallow);
    c.target = false;
    auto release = r.Key('B', false, c);
    CHECK(release.forward && release.swallow);
    CHECK(!r.Key('B', true, c).swallow);
    CHECK(!r.Key('B', false, c).forward);
    c.target = true;
    c.shield = true;
    c.ctrlOnly = true;
    CHECK(r.Key('V', true, c).clipboard);
    c.ctrlOnly = false;
    CHECK(r.Key('V', false, c).clipboard);
    c.guard = true;
    c.hotkey = true;
    CHECK(r.Key('W', true, c).hotkey);
    CHECK(r.Key('W', false, c).swallow);
    c.hotkey = false;
    CHECK(!r.Key('Q', true, c).swallow);
    CHECK(!r.Key('Q', false, c).forward);
    CHECK(!r.Key(999, true, c).swallow);

    // A passed-through key whose release was missed (secure desktop / elevated window):
    // Windows reports it up, so the next press is fresh, not a repeat.
    {
        Router s;
        Context p;  // target none -> Pass
        CHECK(!s.Key('Z', true, p).repeat);
        p.osHeld = false;
        CHECK(!s.Key('Z', true, p).repeat);
        p.osHeld = true;
        CHECK(s.Key('Z', true, p).repeat);  // genuine auto-repeat
    }
    // A stale hotkey key typed without Alt is not a hotkey.
    {
        Router s;
        Context h;
        h.hotkey = true;
        CHECK(s.Key('X', true, h).hotkey);
        Context plain;
        auto r2 = s.Key('X', true, plain);
        CHECK(!r2.hotkey && !r2.swallow && !r2.repeat);
        h.hotkey = true;
        CHECK(s.Key('Y', true, h).hotkey);
        CHECK(s.Key('Y', true, h).hotkey);  // held Alt+Y keeps repeating as a hotkey
    }
    // A stale exclusive key after routing ended is passed through.
    {
        Router s;
        Context e;
        e.target = e.exclusive = true;
        CHECK(s.Key('Q', true, e).swallow);
        e.target = false;
        CHECK(!s.Key('Q', true, e).swallow);
    }
    // Reset forgets everything.
    {
        Router s;
        Context p;
        s.Key('R', true, p);
        s.Reset();
        CHECK(!s.Key('R', true, p).repeat);
    }
    // Stale modifier cleanup: skips the current key and swallowed presses.
    {
        bool down[256] = {}, swallowed[256] = {};
        down[0xA4] = down[0xA2] = down[0xA0] = true;  // LMENU LCONTROL LSHIFT
        swallowed[0xA0] = true;
        ClearStaleKeys(down, swallowed, 0xA2, [](unsigned) { return false; });
        CHECK(!down[0xA4]);  // stale Alt cleared
        CHECK(down[0xA2]);   // current key untouched
        CHECK(down[0xA0]);   // swallowed press untouched
        down[0xA4] = true;
        ClearStaleKeys(down, swallowed, 'X', [](unsigned k) { return k == 0xA4; });
        CHECK(down[0xA4]);   // really held Alt stays
    }
    std::puts("input_logic_test: all checks passed");
}
