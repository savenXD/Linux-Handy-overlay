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
    std::puts("input_logic_test: all checks passed");
}
