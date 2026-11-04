#include "snap_logic.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace cronq::snap_logic;

static int g_failures = 0;
#define CHECK(cond)                                             \
    do {                                                        \
        if (!(cond)) {                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
            g_failures++;                                       \
        }                                                       \
    } while (0)

int main() {
    // 1. PickSnapName
    std::vector<std::string> existing = {"snap-20261022-120000.png", "snap-20261022-120000-1.png",
                                         "other_file.txt"};

    std::string name1 = PickSnapName(existing, "20261022-120001");
    CHECK(name1 == "snap-20261022-120001.png");

    std::string nameColl = PickSnapName(existing, "20261022-120000");
    CHECK(nameColl == "snap-20261022-120000-2.png");

    // 2. FilesToPrune
    std::vector<std::string> folderFiles;
    for (int i = 1; i <= 25; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "snap-20261022-%02d0000.png", i);
        folderFiles.push_back(buf);
    }
    folderFiles.push_back("readme.txt");
    folderFiles.push_back("snap-temp.tmp");

    auto toPrune20 = FilesToPrune(folderFiles, 20);
    CHECK(toPrune20.size() == 5);
    CHECK(toPrune20[0] == "snap-20261022-010000.png");
    CHECK(toPrune20[4] == "snap-20261022-050000.png");

    auto toPruneDefault = FilesToPrune(folderFiles, 0);  // <= 0 defaults to 20
    CHECK(toPruneDefault.size() == 5);

    auto toPrune30 = FilesToPrune(folderFiles, 30);
    CHECK(toPrune30.empty());

    auto collisions =
        FilesToPrune({"snap-20261022-120000-10.png", "snap-20261022-120000.png", "snap-20261022-120000-2.png",
                      "snap-20261022-120000-1.png", "snap-20261022-120001.png", "snap-my-notes.png"},
                     2);
    CHECK(collisions.size() == 3);
    CHECK(collisions[0] == "snap-20261022-120000.png");
    CHECK(collisions[1] == "snap-20261022-120000-1.png");
    CHECK(collisions[2] == "snap-20261022-120000-2.png");

    // 3. Base64Encode
    const uint8_t data1[] = "hello world";
    CHECK(Base64Encode(data1, 11) == "aGVsbG8gd29ybGQ=");

    const uint8_t data2[] = "cronq";
    CHECK(Base64Encode(data2, 5) == "Y3JvbnE=");

    CHECK(Base64Encode(nullptr, 0) == "");

    if (g_failures == 0) {
        std::printf("snap_logic_test: all checks passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
