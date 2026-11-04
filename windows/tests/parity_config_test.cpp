#include "config.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace cronq;

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);        \
            g_failures++;                                              \
        }                                                              \
    } while (0)

int main(int argc, char** argv) {
    std::string path = "shared/fixtures/config_defaults.ini";
    if (argc >= 2) path = argv[1];

    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f.is_open()) {
        std::printf("Failed to open '%s'\n", path.c_str());
        return 2;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    CHECK(!text.empty());

    AppConfig c;
    config::ParseInto(text, &c);

    // [General]
    CHECK(c.width == 360);
    CHECK(c.height == 640);
    CHECK(c.margin == 12);
    CHECK(c.corner == "bottom-left");
    CHECK(c.monitor == "primary");
    CHECK(c.opacity == 1.0);
    CHECK(!c.startVisible);
    CHECK(c.defaultKeyMode == KeyMode::Mirror);

    // [Browser]
    CHECK(c.homeUrl == "https://html5test.com");
    CHECK(c.searchUrlTemplate == "https://duckduckgo.com/?q=%s");
    CHECK(c.userAgent.find("Pixel 8") != std::string::npos);
    CHECK(!c.chatgptAllowlist.empty());

    // [Hotkeys]
    CHECK(c.toggleKey == 'x');
    CHECK(c.modeKey == 'm');
    CHECK(c.ghostKey == 'g');
    CHECK(c.snapKey == 'p');
    CHECK(c.ghostWriterKey == 'w');
    CHECK(c.shieldClipboardKeys == true);

    // [Snap]
    CHECK(c.snapScope == "monitor");
    CHECK(c.snapDelayMs == 80);
    CHECK(c.snapSave == true);
    CHECK(c.snapKeepCount == 20);

    // [GhostWriter]
    CHECK(c.ghostDelayMs == 0);
    CHECK(c.ghostNewline == "space");
    CHECK(c.ghostTab == "space");
    CHECK(c.ghostMaxChars == 100000);

    // Test valid overrides
    std::string overrideText =
        "[General]\n"
        "width = 480\n"
        "height = 800\n"
        "opacity = 0.75\n"
        "default_key_mode = exclusive\n"
        "[Hotkeys]\n"
        "toggle_key = y\n"
        "[Snap]\n"
        "snap_delay_ms = 120\n"
        "snap_keep_count = 50\n";

    AppConfig c2;
    config::ParseInto(overrideText, &c2);
    CHECK(c2.width == 480);
    CHECK(c2.height == 800);
    CHECK(c2.opacity >= 0.74 && c2.opacity <= 0.76);
    CHECK(c2.defaultKeyMode == KeyMode::Exclusive);
    CHECK(c2.toggleKey == 'y');
    CHECK(c2.snapDelayMs == 120);
    CHECK(c2.snapKeepCount == 50);

    // Out-of-range values: must reject and retain default values
    std::string invalidText =
        "[General]\n"
        "width = 99999\n"
        "height = 10\n"
        "opacity = 5.0\n";

    AppConfig c3;
    config::ParseInto(invalidText, &c3);
    CHECK(c3.width == 360);   // retained default
    CHECK(c3.height == 640);  // retained default
    CHECK(c3.opacity == 1.0); // retained default

    if (g_failures == 0) {
        std::printf("all checks passed\n");
        return 0;
    }
    std::printf("%d parity check(s) failed\n", g_failures);
    return 1;
}
