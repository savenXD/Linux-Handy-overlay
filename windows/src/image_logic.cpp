#include "image_logic.h"
#include <cstring>
#include <limits>
namespace cronq {
namespace image_logic {
bool PixelBytes(int w, int h, size_t* out) {
    if (!out || w <= 0 || h <= 0 || uint64_t(w) * uint64_t(h) > kMaxImageBytes / 4) return false;
    *out = size_t(w) * size_t(h) * 4;
    return true;
}
namespace {
uint32_t U32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t U16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
}  // namespace
bool DecodeDib(const uint8_t* p, size_t n, int* w, int* h, std::vector<uint8_t>* out) {
    if (!p || !w || !h || !out || n < 40) return false;
    uint32_t header = U32(p), compression = U32(p + 16), colors = U32(p + 32);
    if (header < 40 || header > n || U16(p + 12) != 1) return false;
    int32_t width = static_cast<int32_t>(U32(p + 4)), height = static_cast<int32_t>(U32(p + 8));
    if (height == std::numeric_limits<int32_t>::min() || height == 0) return false;
    int ah = height < 0 ? -height : height;
    size_t size = 0;
    if (!PixelBytes(width, ah, &size)) return false;
    unsigned bits = U16(p + 14);
    if ((bits != 24 && bits != 32) || (compression != 0 && compression != 3)) return false;
    size_t offset = header;
    if (compression == 3) {
        if (bits != 32) return false;
        size_t maskOffset = 40;
        if (header == 40)
            offset += 12;
        else if (header < 52)
            return false;
        if (maskOffset + 12 > n || U32(p + maskOffset) != 0x00ff0000 ||
            U32(p + maskOffset + 4) != 0x0000ff00 || U32(p + maskOffset + 8) != 0x000000ff)
            return false;
    }
    if (colors > 256 || offset > n || size_t(colors) * 4 > n - offset) return false;
    offset += size_t(colors) * 4;
    size_t stride = ((size_t(width) * bits + 31) / 32) * 4;
    if (offset > n || stride > (n - offset) / size_t(ah)) return false;
    std::vector<uint8_t> result(size);
    for (int y = 0; y < ah; ++y) {
        const auto* row = p + offset + size_t(height > 0 ? ah - 1 - y : y) * stride;
        for (int x = 0; x < width; ++x) {
            size_t src = size_t(x) * (bits / 8), dst = (size_t(y) * width + x) * 4;
            std::memcpy(result.data() + dst, row + src, 3);
            result[dst + 3] = 255;
        }
    }
    *w = width;
    *h = ah;
    *out = std::move(result);
    return true;
}
}  // namespace image_logic
}  // namespace cronq
