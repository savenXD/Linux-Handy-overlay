#pragma once
#include "common.h"
#include <vector>
namespace cronq {
namespace image_codec {
bool EncodePng(int width, int height, const uint8_t* bgra, std::vector<uint8_t>* png);
}
}  // namespace cronq
