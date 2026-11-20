#include "ini_writer.h"

#include <cstdio>
#include <string>

using namespace cronq::ini_writer;

static int g_failures = 0;
#define CHECK(cond)                                             \
    do {                                                        \
        if (!(cond)) {                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
            g_failures++;                                       \
        }                                                       \
    } while (0)

int main() {
    // 1. Update existing key under existing section (with CRLF)
    std::string ini1 =
        "# Global config\r\n"
        "[General]\r\n"
        "width = 360\r\n"
        "height = 640\r\n"
        "\r\n"
        "[Appearance]\r\n"
        "mode = 1\r\n"
        "contrast = 0 # default contrast\r\n"
        "custom = 0\r\n";

    bool ok1 = SetIniInt(ini1, "Appearance", "contrast", 2);
    CHECK(ok1);
    CHECK(ini1.find("contrast = 2 # default contrast") != std::string::npos);
    CHECK(ini1.find("width = 360") != std::string::npos);
    CHECK(ini1.find("\r\n") != std::string::npos);

    // 2. Add new key to existing section
    bool ok2 = SetIniInt(ini1, "Appearance", "extra_opt", 42);
    CHECK(ok2);
    CHECK(ini1.find("extra_opt = 42") != std::string::npos);

    // 3. Add new section and key to INI without the section
    std::string ini2 =
        "[General]\n"
        "width = 500\n";
    bool ok3 = SetIniInt(ini2, "Appearance", "mode", 1);
    CHECK(ok3);
    CHECK(ini2.find("[Appearance]\nmode = 1\n") != std::string::npos);

    // 4. Preserve exact formatting and a UTF-8 BOM.

    std::string exact = "\xEF\xBB\xBF[Appearance]\r\n\tcontrast\t=\t0   # preserve me  \r\n";
    CHECK(SetIniInt(exact, "Appearance", "contrast", 2));
    CHECK(exact == "\xEF\xBB\xBF[Appearance]\r\n\tcontrast\t=\t2   # preserve me  \r\n");
    std::string corner="[General]\r\ncorner = bottom-left  # saved position\r\n[Appearance]\r\nmode = 2\r\n";
    CHECK(SetIniString(corner,"General","corner","bottom-right"));
    CHECK(corner=="[General]\r\ncorner = bottom-right  # saved position\r\n[Appearance]\r\nmode = 2\r\n");
    CHECK(!SetIniString(corner,"General","corner","bottom-left\nmode=0"));
    if (g_failures == 0) {
        std::printf("ini_writer_test: all checks passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
