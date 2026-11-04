#include "log_rotate.h"

namespace cronq {
namespace log_rotate {

bool ShouldRotate(uint64_t currentSize, uint64_t maxSize) {
    if (maxSize == 0) return false;
    return currentSize > maxSize;
}

}  // namespace log_rotate
}  // namespace cronq
