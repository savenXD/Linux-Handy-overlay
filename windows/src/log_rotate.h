#pragma once

#include <cstdint>

namespace cronq {
namespace log_rotate {

constexpr uint64_t kDefaultMaxLogBytes = 2u * 1024u * 1024u;  // 2 MB

// Pure function deciding whether log rotation should occur.
// Rotates when currentSize strictly exceeds maxSize and maxSize > 0.
bool ShouldRotate(uint64_t currentSize, uint64_t maxSize = kDefaultMaxLogBytes);

}  // namespace log_rotate
}  // namespace cronq
