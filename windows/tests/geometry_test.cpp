#include "geometry.h"

#include <cstdio>

using namespace cronq::geometry;

static int g_failures = 0;
#define CHECK(cond)                                             \
    do {                                                        \
        if (!(cond)) {                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
            g_failures++;                                       \
        }                                                       \
    } while (0)

int main() {
    // 1. Standard 1920x1080 monitor with a 40px taskbar at the bottom (work area: 0, 0, 1920, 1040)
    Rect bottomBar{0, 0, 1920, 1040};
    int w = 360, h = 640, m = 12;
    int x = 0, y = 0;

    // Bottom-Left
    CalcCorner(bottomBar, w, h, m, "bottom-left", &x, &y);
    CHECK(x == 12 && y == (1040 - 640 - 12));  // 388

    // Bottom-Right
    CalcCorner(bottomBar, w, h, m, "bottom-right", &x, &y);
    CHECK(x == (1920 - 360 - 12) && y == (1040 - 640 - 12));  // x=1548, y=388

    // Top-Left
    CalcCorner(bottomBar, w, h, m, "top-left", &x, &y);
    CHECK(x == 12 && y == 12);

    // Top-Right
    CalcCorner(bottomBar, w, h, m, "top-right", &x, &y);
    CHECK(x == (1920 - 360 - 12) && y == 12);  // x=1548, y=12

    // Case-insensitivity and fallback
    CalcCorner(bottomBar, w, h, m, "BOTTOM-RIGHT", &x, &y);
    CHECK(x == 1548 && y == 388);

    CalcCorner(bottomBar, w, h, m, "invalid-corner", &x, &y);
    CHECK(x == 12 && y == 388);  // defaults to bottom-left

    CalcCorner(bottomBar, w, h, m, "", &x, &y);
    CHECK(x == 12 && y == 388);  // defaults to bottom-left

    // 2. Taskbar at top (work area: 0, 40, 1920, 1080)
    Rect topBar{0, 40, 1920, 1080};
    CalcCorner(topBar, w, h, m, "top-left", &x, &y);
    CHECK(x == 12 && y == (40 + 12));  // 52

    // 3. Taskbar at left (work area: 60, 0, 1920, 1080)
    Rect leftBar{60, 0, 1920, 1080};
    CalcCorner(leftBar, w, h, m, "bottom-left", &x, &y);
    CHECK(x == (60 + 12) && y == (1080 - 640 - 12));  // x=72, y=428

    // 4. Taskbar at right (work area: 0, 0, 1860, 1080)
    Rect rightBar{0, 0, 1860, 1080};
    CalcCorner(rightBar, w, h, m, "bottom-right", &x, &y);
    CHECK(x == (1860 - 360 - 12) && y == (1080 - 640 - 12));  // x=1488, y=428

    // 5. Secondary monitor offset to the right (work area: 1920, 0, 3840, 1080)
    Rect secondMon{1920, 0, 3840, 1080};
    CalcCorner(secondMon, w, h, m, "bottom-left", &x, &y);
    CHECK(x == (1920 + 12) && y == (1080 - 640 - 12));

    // 6. Monitor index boundary / fallback tests
    CHECK(SafeMonitorIndex(0, 2) == 0);
    CHECK(SafeMonitorIndex(1, 2) == 1);
    CHECK(SafeMonitorIndex(2, 2) == 0);   // out of bounds -> fallback to 0
    CHECK(SafeMonitorIndex(99, 2) == 0);  // out of bounds -> fallback to 0
    CHECK(SafeMonitorIndex(-1, 2) == 0);  // negative -> fallback to 0
    CHECK(SafeMonitorIndex(99, 2, 1) == 1);
    CHECK(SafeMonitorIndex(0, 0) == 0);  // 0 monitors -> 0

    // Scaled laptop: do not consume nearly the full usable screen height.
    w = 450; h = 800;
    FitPhone({0, 0, 1920, 1020}, 15, &w, &h);
    CHECK(w == 417 && h == 742);
    CalcCorner({0, 0, 1920, 1020}, w, h, 15, "bottom-left", &x, &y);
    CHECK(x == 15 && y == 263);
    // Negative coordinates and small work areas stay on their own monitor.
    w = 720; h = 1280;
    FitPhone({-1280, -720, 0, 0}, 24, &w, &h);
    CHECK(w <= 1232 && h <= 504);
    CalcCorner({-1280, -720, 0, 0}, w, h, 24, "top-right", &x, &y);
    CHECK(x >= -1280 && y >= -720 && x + w <= 0 && y + h <= 0);
    CalcCorner({-800, -600, 0, 0}, 900, 700, 12, "bottom-right", &x, &y);
    CHECK(x == -800 && y == -600);

    if (g_failures == 0) {
        std::printf("all checks passed\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
