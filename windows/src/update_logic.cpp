#include "update_logic.h"

#include <cctype>
#include <vector>

namespace cronq {
namespace update_logic {

namespace {
std::vector<long> Parts(const std::string& v) {
    std::vector<long> out;
    size_t i = (!v.empty() && (v[0] == 'v' || v[0] == 'V')) ? 1 : 0;
    while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) {
        long n = 0;
        while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i])) && n < 100000000)
            n = n * 10 + (v[i++] - '0');
        out.push_back(n);
        if (i < v.size() && v[i] == '.') ++i; else break;
    }
    return out;
}
}  // namespace

int CompareVersions(const std::string& a, const std::string& b) {
    auto x = Parts(a), y = Parts(b);
    while (x.size() < y.size()) x.push_back(0);
    while (y.size() < x.size()) y.push_back(0);
    for (size_t i = 0; i < x.size(); ++i)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

std::string TagName(const std::string& json) {
    const std::string key = "\"tag_name\"";
    size_t at = json.find(key);
    if (at == std::string::npos) return {};
    at = json.find(':', at + key.size());
    if (at == std::string::npos) return {};
    at = json.find('"', at);
    if (at == std::string::npos) return {};
    size_t end = json.find('"', at + 1);
    if (end == std::string::npos || end - at - 1 > 64) return {};
    std::string tag = json.substr(at + 1, end - at - 1);
    for (char c : tag)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '_') return {};
    return tag;
}

std::string Sha256For(const std::string& sums, const std::string& fileName) {
    size_t pos = 0;
    while (pos < sums.size()) {
        size_t eol = sums.find('\n', pos);
        if (eol == std::string::npos) eol = sums.size();
        std::string line = sums.substr(pos, eol - pos);
        pos = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.size() < 66) continue;
        std::string hex = line.substr(0, 64), rest = line.substr(64);
        size_t s = rest.find_first_not_of(" *");
        if (s == std::string::npos || rest.substr(s) != fileName) continue;
        bool ok = true;
        for (char& c : hex) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!std::isxdigit(static_cast<unsigned char>(c))) ok = false;
        }
        if (ok) return hex;
    }
    return {};
}

}  // namespace update_logic
}  // namespace cronq
