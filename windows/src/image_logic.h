#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace cronq {
namespace image_logic {
constexpr size_t kMaxImageBytes = 256u * 1024u * 1024u;
bool PixelBytes(int width, int height, size_t* out);
bool DecodeDib(const uint8_t* bytes, size_t size, int* width, int* height, std::vector<uint8_t>* bgra);
}  // namespace image_logic
}  // namespace cronq
