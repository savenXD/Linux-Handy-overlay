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
        "[GhostTiming]\r\n"
        "human = 1\r\n"
        "speed = 0 # default speed\r\n"
        "safe = 0\r\n";

    bool ok1 = SetIniInt(ini1, "GhostTiming", "speed", 2);
    CHECK(ok1);
    CHECK(ini1.find("speed = 2 # default speed") != std::string::npos);
    CHECK(ini1.find("width = 360") != std::string::npos);
    CHECK(ini1.find("\r\n") != std::string::npos);
    CHECK(GetIniInt(ini1, "GhostTiming", "speed", -1) == 2);
    CHECK(GetIniInt(ini1, "GhostTiming", "human", -1) == 1);
    CHECK(GetIniInt(ini1, "GhostTiming", "safe", -1) == 0);
    CHECK(GetIniInt(ini1, "General", "width", -1) == 360);

    // 2. Add new key to existing section
    bool ok2 = SetIniInt(ini1, "GhostTiming", "extra_opt", 42);
    CHECK(ok2);
    CHECK(GetIniInt(ini1, "GhostTiming", "extra_opt", -1) == 42);

    // 3. Add new section and key to INI without the section
    std::string ini2 =
        "[General]\n"
        "width = 500\n";
    bool ok3 = SetIniInt(ini2, "GhostTiming", "human", 1);
    CHECK(ok3);
    CHECK(ini2.find("[GhostTiming]\nhuman = 1\n") != std::string::npos);
    CHECK(GetIniInt(ini2, "GhostTiming", "human", -1) == 1);

    // 4. Default value on missing key
    CHECK(GetIniInt(ini2, "NonExistent", "missing", 999) == 999);

    std::string exact = "\xEF\xBB\xBF[GhostTiming]\r\n\tspeed\t=\t0   # preserve me  \r\n";
    CHECK(GetIniInt(exact, "GhostTiming", "speed", -1) == 0);
    CHECK(SetIniInt(exact, "GhostTiming", "speed", 2));
    CHECK(exact == "\xEF\xBB\xBF[GhostTiming]\r\n\tspeed\t=\t2   # preserve me  \r\n");
    CHECK(GetIniInt("[s]\nk=12junk\n", "s", "k", 7) == 7);
    CHECK(GetIniInt("[s]\nk=999999999999999999999999\n", "s", "k", 7) == 7);
    if (g_failures == 0) {
        std::printf("ini_writer_test: all checks passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
