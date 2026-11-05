// Pure decisions for the kill-switch purge (no Win32, unit-testable). See purge.cpp for the actions.
#pragma once

#include <string>
#include <vector>

namespace cronq {
namespace purge_logic {

// Case-insensitive ASCII helpers on UTF-16 text.
std::wstring Lower(const std::wstring& s);
bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle);
bool EqualsNoCase(const std::wstring& a, const std::wstring& b);
std::wstring TrimSlashes(const std::wstring& path);

// A Run/RunOnce value that starts Cronq: named "Cronq" or pointing at a cronq executable.
bool IsCronqRunValue(const std::wstring& name, const std::wstring& data);

// A shortcut file name Cronq created (Start Menu / Desktop / Startup).
bool IsCronqShortcut(const std::wstring& fileName);

// The user PATH with every entry equal to installDir removed (order and other entries kept).
// Returns false when nothing needed to change.
bool RemovePathEntry(const std::wstring& path, const std::wstring& installDir, std::wstring* updated);

// Inside a portable / developer folder only Cronq's own files are removed, never the folder's other
// contents. The standard install folder is removed whole.
bool RemoveWholeInstallDir(const std::wstring& installDir, const std::wstring& standardDir);
const std::vector<std::wstring>& OwnFileNames();
bool IsOwnFile(const std::wstring& fileName);  // OwnFileNames() or an uninstaller (unins*)

}  // namespace purge_logic
}  // namespace cronq
