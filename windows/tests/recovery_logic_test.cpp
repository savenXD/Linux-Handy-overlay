#include <cstdio>

#include "recovery_logic.h"

using namespace cronq::recovery_logic;
#define CHECK(x)                                        \
    do {                                                \
        if (!(x)) {                                     \
            std::printf("FAIL %d: %s\n", __LINE__, #x); \
            return 1;                                   \
        }                                               \
    } while (0)

int main() {
    CHECK(!ShouldRestart(0));
    CHECK(!ShouldRestart(kStatusControlCExit));
    CHECK(!ShouldRestart(kExitNoRestart));
    CHECK(ShouldRestart(1));
    CHECK(ShouldRestart(0xC0000005));  // access violation

    // Supervisor pacing: 2 s, then 4, 8, 16, 32, 60, 60 ... while crashes keep coming quickly.
    Backoff s(2000, 4000, 60000, 60000);
    int64_t t = 0;
    CHECK(s.Next(t) == 2000);
    CHECK(s.Next(t += 5000) == 4000);
    CHECK(s.Next(t += 5000) == 8000);
    CHECK(s.Next(t += 5000) == 16000);
    CHECK(s.Next(t += 5000) == 32000);
    CHECK(s.Next(t += 5000) == 60000);
    CHECK(s.Next(t += 5000) == 60000);
    for (int i = 0; i < 100; ++i) s.Next(t += 1000);  // no overflow on long streaks
    CHECK(s.Next(t += 1000) == 60000);
    // A crash after a quiet hour starts over.
    CHECK(s.Next(t += 3600000) == 2000);
    CHECK(s.Streak() == 0);

    // Browser recovery: first immediately, then 2 s, 4 s ...
    Backoff b(0, 2000, 60000, 30000);
    CHECK(b.Next(0) == 0);
    CHECK(b.Next(100) == 2000);
    CHECK(b.Next(2200) == 4000);
    CHECK(b.Next(100000) == 0);
    std::puts("recovery_logic_test: all checks passed");
}
