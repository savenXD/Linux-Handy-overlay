#include "capture_logic.h"

#include <cstdio>
#include <limits>
using namespace cronq::capture_logic;
#define CHECK(x)                                        \
    do {                                                \
        if (!(x)) {                                     \
            std::printf("FAIL %d: %s\n", __LINE__, #x); \
            return 1;                                   \
        }                                               \
    } while (0)

int main() {
    CHECK(!IsBlack({}));
    CHECK(!IsBlack({0, 0, 0}));
    CHECK(IsBlack({0, 0, 0, 255, 0, 0, 0, 0}));  // alpha cannot hide black RGB
    CHECK(!IsBlack({0, 0, 0, 0, 0, 1, 0, 0}));   // preserve genuinely dark content

    // Asymmetric 3x2 surface with padded GPU rows and deliberately undefined
    // alpha.
    const uint8_t data[] = {1, 11, 21, 0, 2, 12, 22, 0, 3, 13, 23, 0, 99, 99, 99, 99,
                            4, 14, 24, 0, 5, 15, 25, 0, 6, 16, 26, 0, 99, 99, 99, 99};
    const Rotation rotations[] = {Rotation::Identity, Rotation::Rotate90, Rotation::Rotate180,
                                  Rotation::Rotate270};
    const uint8_t expected[][6] = {
        {1, 2, 3, 4, 5, 6}, {4, 1, 5, 2, 6, 3}, {6, 5, 4, 3, 2, 1}, {3, 6, 2, 5, 1, 4}};
    std::vector<uint8_t> out;
    for (int r = 0; r < 4; ++r) {
        CHECK(ReadSurface(data, sizeof(data), 3, 2, 16, rotations[r], &out));
        CHECK(out.size() == 24);
        for (int i = 0; i < 6; ++i) {
            CHECK(out[i * 4] == expected[r][i]);
            CHECK(out[i * 4 + 1] == expected[r][i] + 10);
            CHECK(out[i * 4 + 2] == expected[r][i] + 20);
            CHECK(out[i * 4 + 3] == 255);
        }
    }
    auto previous = out;
    CHECK(!ReadSurface(nullptr, sizeof(data), 3, 2, 16, Rotation::Identity, &out));
    CHECK(!ReadSurface(data, sizeof(data), 0, 2, 16, Rotation::Identity, &out));
    CHECK(!ReadSurface(data, 31, 3, 2, 16, Rotation::Identity, &out));
    CHECK(!ReadSurface(data, sizeof(data), 3, 2, 11, Rotation::Identity, &out));
    CHECK(
        !ReadSurface(data, sizeof(data), 3, 2, std::numeric_limits<size_t>::max(), Rotation::Identity, &out));
    CHECK(!ReadSurface(data, sizeof(data), 3, 2, 16, static_cast<Rotation>(99), &out));
    CHECK(out == previous);  // malformed surfaces never replace the caller's valid frame
    std::puts("capture_logic_test: all checks passed");
}
