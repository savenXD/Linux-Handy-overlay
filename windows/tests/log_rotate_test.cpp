#include "log_rotate.h"

#include <cstdio>

using namespace cronq::log_rotate;

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);        \
            g_failures++;                                              \
        }                                                              \
    } while (0)

int main() {
    constexpr uint64_t k2MB = 2u * 1024u * 1024u;

    // Zero / below threshold: do not rotate
    CHECK(!ShouldRotate(0, k2MB));
    CHECK(!ShouldRotate(100, k2MB));
    CHECK(!ShouldRotate(k2MB - 1, k2MB));
    CHECK(!ShouldRotate(k2MB, k2MB));  // boundary: exact limit does not trigger rotate

    // Above threshold: rotate
    CHECK(ShouldRotate(k2MB + 1, k2MB));
    CHECK(ShouldRotate(k2MB * 2, k2MB));
    CHECK(ShouldRotate(100ULL * 1024ULL * 1024ULL, k2MB));

    // Custom limit
    CHECK(!ShouldRotate(500, 1000));
    CHECK(!ShouldRotate(1000, 1000));
    CHECK(ShouldRotate(1001, 1000));

    // Zero limit disables rotation
    CHECK(!ShouldRotate(100, 0));
    CHECK(!ShouldRotate(0, 0));

    // Default parameter
    CHECK(!ShouldRotate(k2MB));
    CHECK(ShouldRotate(k2MB + 1));

    if (g_failures == 0) {
        std::printf("all checks passed\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
