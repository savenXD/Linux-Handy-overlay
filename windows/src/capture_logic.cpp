#include "capture_logic.h"

#include <cstring>

#include "image_logic.h"

namespace cronq {
namespace capture_logic {
bool IsBlack(const std::vector<uint8_t>& bgra) {
    if (bgra.empty() || bgra.size() % 4) return false;
    for (size_t i = 0; i < bgra.size(); i += 4)
        if (bgra[i] || bgra[i + 1] || bgra[i + 2]) return false;
    return true;
}

bool ReadSurface(const uint8_t* data, size_t size, int w, int h, size_t pitch, Rotation rotation,
                 std::vector<uint8_t>* out) {
    size_t bytes = 0;
    if (!data || !out || !image_logic::PixelBytes(w, h, &bytes) || pitch < size_t(w) * 4 ||
        size / size_t(h) < pitch)
        return false;
    const bool swapped = rotation == Rotation::Rotate90 || rotation == Rotation::Rotate270;
    int dw = swapped ? h : w;
    std::vector<uint8_t> result(bytes);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int dx = x, dy = y;
            switch (rotation) {
                case Rotation::Identity:
                    break;
                case Rotation::Rotate90:
                    dx = h - 1 - y;
                    dy = x;
                    break;
                case Rotation::Rotate180:
                    dx = w - 1 - x;
                    dy = h - 1 - y;
                    break;
                case Rotation::Rotate270:
                    dx = y;
                    dy = w - 1 - x;
                    break;
                default:
                    return false;
            }
            auto* dst = result.data() + (size_t(dy) * dw + dx) * 4;
            std::memcpy(dst, data + size_t(y) * pitch + size_t(x) * 4, 3);
            dst[3] = 255;
        }
    }
    *out = std::move(result);
    return true;
}
}  // namespace capture_logic
}  // namespace cronq
