#include <cstdio>

#include "update_logic.h"

using namespace cronq::update_logic;
#define CHECK(x)                                        \
    do {                                                \
        if (!(x)) {                                     \
            std::printf("FAIL %d: %s\n", __LINE__, #x); \
            return 1;                                   \
        }                                               \
    } while (0)

int main() {
    CHECK(CompareVersions("v1.2.0", "1.1.1") > 0);
    CHECK(CompareVersions("1.1.1", "v1.1.1") == 0);
    CHECK(CompareVersions("1.1", "1.1.0") == 0);
    CHECK(CompareVersions("1.10.0", "1.9.9") > 0);  // numeric, not text
    CHECK(CompareVersions("1.2.0-beta", "1.2.0") == 0);
    CHECK(CompareVersions("0.9", "1.0") < 0);

    CHECK(TagName(R"({"url":"x","tag_name": "v1.2.0","name":"v1.2.0"})") == "v1.2.0");
    CHECK(TagName(R"({"message":"Not Found"})").empty());
    CHECK(TagName(R"({"tag_name":"v1;rm -rf"})").empty());  // only version-like tags

    const std::string sums =
        "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef  cronq-setup.exe\r\n"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff  other.exe\n";
    CHECK(Sha256For(sums, "cronq-setup.exe") == "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    CHECK(Sha256For(sums, "other.exe").size() == 64);
    CHECK(Sha256For(sums, "missing.exe").empty());
    CHECK(Sha256For("short  cronq-setup.exe", "cronq-setup.exe").empty());
    std::puts("update_logic_test: all checks passed");
}
