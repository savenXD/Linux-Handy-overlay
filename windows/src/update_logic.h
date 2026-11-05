// Pure helpers for `cronq update` (no Win32, unit-testable).
#pragma once

#include <string>

namespace cronq {
namespace update_logic {

// "v1.10.2" / "1.10.2" -> compares numerically, part by part. <0 if a<b, 0 equal, >0 if a>b.
// Text after the numbers (e.g. "-beta") is ignored.
int CompareVersions(const std::string& a, const std::string& b);

// "tag_name" from GitHub's releases/latest JSON; empty if absent.
std::string TagName(const std::string& json);

// The lower-case SHA-256 hex for fileName from a SHA256SUMS.txt ("<hex>  <name>" lines); empty if absent.
std::string Sha256For(const std::string& sums, const std::string& fileName);

}  // namespace update_logic
}  // namespace cronq
