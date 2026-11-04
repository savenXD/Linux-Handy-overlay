#include "image_logic.h"
#include <cstdio>
using namespace cronq::image_logic;
#define CHECK(x)                                        \
    do {                                                \
        if (!(x)) {                                     \
            std::printf("FAIL %d: %s\n", __LINE__, #x); \
            return 1;                                   \
        }                                               \
    } while (0)
int main() {
    size_t n = 0;
    CHECK(PixelBytes(2, 2, &n) && n == 16);
    CHECK(!PixelBytes(-1, 2, &n));
    CHECK(!PixelBytes(100000, 100000, &n));
    std::vector<uint8_t> dib(48);
    dib[0] = 40;
    dib[4] = 1;
    dib[8] = 2;
    dib[12] = 1;
    dib[14] = 24;
    dib[40] = 1;
    dib[41] = 2;
    dib[42] = 3;
    dib[44] = 4;
    dib[45] = 5;
    dib[46] = 6;
    std::vector<uint8_t> pixels;
    int w = 0, h = 0;
    CHECK(DecodeDib(dib.data(), dib.size(), &w, &h, &pixels));
    CHECK(w == 1 && h == 2 && pixels[0] == 4 && pixels[4] == 1 && pixels[3] == 255);
    CHECK(!DecodeDib(dib.data(), 44, &w, &h, &pixels));
    dib[14] = 8;
    CHECK(!DecodeDib(dib.data(), dib.size(), &w, &h, &pixels));
    dib[14] = 32;
    dib[8] = 255;
    dib[9] = 255;
    dib[10] = 255;
    dib[11] = 255;
    CHECK(DecodeDib(dib.data(), dib.size(), &w, &h, &pixels));
    CHECK(h == 1 && pixels[0] == 1);
    dib[11] = 128;
    dib[8] = 0;
    dib[9] = 0;
    dib[10] = 0;
    CHECK(!DecodeDib(dib.data(), dib.size(), &w, &h, &pixels));
    std::puts("image_logic_test: all checks passed");
}
