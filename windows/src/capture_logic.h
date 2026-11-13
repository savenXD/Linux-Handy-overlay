#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cronq {
namespace capture_logic {
// Clockwise rotation from the duplication surface to the displayed desktop.
enum class Rotation { Identity, Rotate90, Rotate180, Rotate270 };
bool IsBlack(const std::vector<uint8_t>& bgra);
// Converts padded GPU rows to tightly packed, opaque BGRA in display
// orientation.
bool ReadSurface(const uint8_t* data, size_t size, int width, int height, size_t pitch, Rotation rotation,
                 std::vector<uint8_t>* out);
}  // namespace capture_logic
}  // namespace cronq
