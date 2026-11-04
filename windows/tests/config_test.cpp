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

    // 1. The shared default file parses to the built-in defaults.
    AppConfig c;
    config::ParseInto(ss.str(), &c);
    CHECK(c.width == 360 && c.height == 640 && c.margin == 12);
    CHECK(c.corner == "bottom-left" && c.monitor == "primary");
    CHECK(c.opacity == 1.0 && !c.startVisible && c.defaultKeyMode == KeyMode::Mirror);
    CHECK(c.homeUrl == "https://html5test.com");
    CHECK(c.searchUrlTemplate == "https://duckduckgo.com/?q=%s");
    CHECK(c.userAgent.find("Pixel 8") != std::string::npos);
    CHECK(c.toggleKey == 'x' && c.modeKey == 'm' && c.ghostKey == 'g' && c.snapKey == 'p' && c.ghostWriterKey == 'w');
    CHECK(c.snapScope == "monitor" && c.snapDelayMs == 80 && c.snapSave && c.snapKeepCount == 20);
    CHECK(c.ghostDelayMs == 0 && c.ghostNewline == "space" && c.ghostTab == "space" && c.ghostMaxChars == 100000);

    // 2. Overrides, CRLF line endings, a UTF-8 BOM and the section fallbacks.
    AppConfig o;
    config::ParseInto(
        "\xEF\xBB\xBF[General]\r\nwidth = 500\r\nopacity = 0.5\r\nstart_visible = true\r\ndefault_key_mode = Exclusive\r\n"
        "corner = top-right\r\n[Hotkeys]\r\ntoggle_key = z\r\n[Snap]\r\nsnap_key = k\r\n"
        "[GhostWriter]\r\nghost_writer_key = j\r\nghost_newline = return\r\n",
        &o);
    CHECK(o.width == 500 && o.height == 640);
    CHECK(o.opacity == 0.5 && o.startVisible && o.defaultKeyMode == KeyMode::Exclusive);
    CHECK(o.corner == "top-right" && o.toggleKey == 'z');
    CHECK(o.snapKey == 'k' && o.ghostWriterKey == 'j' && o.ghostNewline == "return");

    // 3. Out-of-range or malformed values are ignored (defaults stay).
    AppConfig bad;
    config::ParseInto("[General]\nwidth = 99999\nheight = abc\nmargin = -5\nopacity = 5\n[Snap]\nsnap_delay_ms = 10\n"
                      "snap_keep_count = 0\n[GhostWriter]\nghost_max_chars = 9999999\n",
                      &bad);
    CHECK(bad.width == 360 && bad.height == 640 && bad.margin == 12 && bad.opacity == 1.0);
    CHECK(bad.snapDelayMs == 80 && bad.snapKeepCount == 20 && bad.ghostMaxChars == 100000);

    // 4. '#' comments and values containing '=' / '%s'.
    AppConfig q;
    config::ParseInto("# comment\n[Browser]\n# home_url = https://ignored\nsearch_url_template = https://x.test/?a=1&q=%s\n", &q);
    CHECK(q.homeUrl == "https://html5test.com");
    CHECK(q.searchUrlTemplate == "https://x.test/?a=1&q=%s");

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
