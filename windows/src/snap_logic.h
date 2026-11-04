#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cronq {
namespace snap_logic {

// Computes the unique filename for a snapshot given the timestamp string (YYYYMMDD-HHMMSS)
// and a list of existing filenames in the directory.
// Returns "snap-<stamp>.png", or "snap-<stamp>-1.png", "-2.png", etc. if collisions exist.
std::string PickSnapName(const std::vector<std::string>& existingNames, const std::string& stamp);

// Determines which files should be deleted from the snapshot folder given existing filenames and keepCount.
// Filters for names matching "snap-*.png", sorts ascending, and returns the oldest entries exceeding keepCount.
// If keepCount <= 0, default 20 is used.
std::vector<std::string> FilesToPrune(const std::vector<std::string>& existingNames, int keepCount);

// Standard RFC 4648 Base64 encoder for image payloads.
std::string Base64Encode(const uint8_t* data, size_t len);

}  // namespace snap_logic
}  // namespace cronq
