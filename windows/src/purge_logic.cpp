#include "purge_logic.h"

namespace cronq {
namespace purge_logic {

std::wstring Lower(const std::wstring& s) {
    std::wstring out = s;
    for (auto& c : out)
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    return out;
}
bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle) {
    return Lower(haystack).find(Lower(needle)) != std::wstring::npos;
}
bool EqualsNoCase(const std::wstring& a, const std::wstring& b) { return Lower(a) == Lower(b); }
std::wstring TrimSlashes(const std::wstring& path) {
    std::wstring p = path;
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/' || p.back() == L' ')) p.pop_back();
    size_t b = 0;
    while (b < p.size() && p[b] == L' ') ++b;
    return p.substr(b);
}

bool IsCronqRunValue(const std::wstring& name, const std::wstring& data) {
    if (EqualsNoCase(name, L"Cronq")) return true;
    const std::wstring d = Lower(data);
    for (const wchar_t* exe : {L"cronq.exe", L"cronqw.exe", L"cronqw_uia.exe"}) {
        const size_t at = d.find(exe);
        // Only an executable named exactly like ours: "...\cronqw.exe" or "cronqw.exe" at the start.
        if (at != std::wstring::npos && (at == 0 || d[at - 1] == L'\\' || d[at - 1] == L'/' || d[at - 1] == L'"'))
            return true;
    }
    return false;
}

bool IsCronqShortcut(const std::wstring& fileName) {
    const std::wstring f = Lower(fileName);
    return f.size() > 4 && f.compare(f.size() - 4, 4, L".lnk") == 0 && f.find(L"cronq") != std::wstring::npos;
}

bool RemovePathEntry(const std::wstring& path, const std::wstring& installDir, std::wstring* updated) {
    const std::wstring target = TrimSlashes(installDir);
    std::wstring out;
    bool changed = false;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(L';', start);
        if (end == std::wstring::npos) end = path.size();
        const std::wstring part = path.substr(start, end - start);
        if (!target.empty() && EqualsNoCase(TrimSlashes(part), target)) {
            changed = true;
        } else if (!part.empty()) {
            if (!out.empty()) out += L';';
            out += part;
        }
        start = end + 1;
    }
    if (updated) *updated = out;
    return changed;
}

bool RemoveWholeInstallDir(const std::wstring& installDir, const std::wstring& standardDir) {
    return !standardDir.empty() && EqualsNoCase(TrimSlashes(installDir), TrimSlashes(standardDir));
}

const std::vector<std::wstring>& OwnFileNames() {
    static const std::vector<std::wstring> names = {L"cronq.exe", L"cronqw.exe", L"cronqw_uia.exe",
                                                    L"WebView2Loader.dll", L"killswitch.ps1", L"ui"};
    return names;
}

bool IsOwnFile(const std::wstring& fileName) {
    for (const auto& n : OwnFileNames())
        if (EqualsNoCase(fileName, n)) return true;
    return Lower(fileName).rfind(L"unins", 0) == 0;
}

}  // namespace purge_logic
}  // namespace cronq
