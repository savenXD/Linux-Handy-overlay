// Checks that the Windows config parser applies the same defaults and validation rules as src/config.c.
#include "config.h"

#include <cstdio>
#include <fstream>
#include <sstream>

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
    if (argc < 2) {
        std::printf("usage: config_test <config_defaults.ini>\n");
        return 2;
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    CHECK(!ss.str().empty());

    // Privacy: capture protection is ON by default, with or without the key in an older config.ini.
    {
        AppConfig d;
        CHECK(d.hideFromCapture);
        AppConfig old;
        config::ParseInto("[General]\nwidth = 360\n", &old);
        CHECK(old.hideFromCapture);
        AppConfig off;
        config::ParseInto("[Privacy]\nhide_from_capture = false\n", &off);
        CHECK(!off.hideFromCapture);
    }

    // 1. The Windows default file parses to the built-in defaults.
    AppConfig c;
    config::ParseInto(ss.str(), &c);
    CHECK(c.width == 360 && c.height == 640 && c.margin == 12);
    CHECK(c.corner == "bottom-left" && c.monitor == "primary");
    CHECK(c.opacity == 1.0 && !c.startVisible);
    CHECK(c.snapScope == "monitor" && c.snapDelayMs == 80 && c.snapSave && c.snapKeepCount == 20);

    CHECK(c.toggleKey == 'x' && c.modeKey == 'm' && c.ghostKey == 'g' && c.snapKey == 'p');

    // 2. Overrides, CRLF line endings, a UTF-8 BOM and the section fallbacks.
    AppConfig o;
    config::ParseInto(
        "\xEF\xBB\xBF[General]\r\nwidth = 500\r\nopacity = 0.5\r\nstart_visible = true\r\n"
        "corner = top-right\r\n[Hotkeys]\r\ntoggle_key = z\r\n[Snap]\r\nsnap_key = k\r\n",
        &o);
    CHECK(o.width == 500 && o.height == 640);
    CHECK(o.opacity == 0.5 && o.startVisible);
    CHECK(o.corner == "top-right" && o.toggleKey == 'z');

    CHECK(o.snapKey == 'k');

    // 3. Out-of-range or malformed values are ignored (defaults stay).
    AppConfig bad;
    config::ParseInto("[General]\nwidth = 99999\nheight = abc\nmargin = -5\nopacity = 5\n[Snap]\nsnap_delay_ms = 10\n"
                      "snap_keep_count = 0\n",
                      &bad);
    CHECK(bad.width == 360 && bad.height == 640 && bad.margin == 12 && bad.opacity == 1.0);

    CHECK(bad.snapDelayMs == 80 && bad.snapKeepCount == 20);

    // 4. Comments and ChatGPT allowlist override.
    AppConfig q;
    config::ParseInto("# comment\n[ChatGPT]\n# allowlist = ignored\nallowlist = chatgpt.com,example.test\n", &q);

    CHECK(q.chatgptAllowlist == "chatgpt.com,example.test");

    AppConfig legacy;
    config::ParseInto("[Browser]\nchatgpt_allowlist=legacy.test\n", &legacy);
    CHECK(legacy.chatgptAllowlist == "legacy.test");
    config::ParseInto("[Browser]\nchatgpt_allowlist=legacy.test\n[ChatGPT]\nallowlist=new.test\n", &legacy);
    CHECK(legacy.chatgptAllowlist == "new.test");

    config::ParseInto("[Appearance]\nmode=2\n", &q);
    CHECK(q.appearanceMode == 2);
    config::ParseInto("[Appearance]\nmode=9\n", &q);
    CHECK(q.appearanceMode == 2);
    config::ParseInto("[Appearance]\nmode=1\n", &q);
    CHECK(q.appearanceMode == 1);
    config::ParseInto("[Appearance]\nmode=3\n", &q);
    CHECK(q.appearanceMode == 3);
    if (g_failures == 0) std::printf("config_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
