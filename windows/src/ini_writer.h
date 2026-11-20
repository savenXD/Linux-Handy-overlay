#pragma once

#include <string>

namespace cronq {
namespace ini_writer {

// Updates an integer key in an INI string in-place under the given section.
// If the key exists in that section, its value is replaced in-place.
// If the section exists but the key does not, the key is inserted before the next section
// or at the end of the section.
// If the section does not exist, a new section and key are appended at the end.
// Preserves all comments, whitespace, and line-ending conventions (CRLF or LF).
bool SetIniInt(std::string& text, const char* section, const char* key, int value);
bool SetIniString(std::string& text, const char* section, const char* key, const std::string& value);

}  // namespace ini_writer
}  // namespace cronq
