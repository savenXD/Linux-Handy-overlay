#include "snap_logic.h"

#include <algorithm>
#include <set>
#include <charconv>
#include <cstdint>

namespace cronq {
namespace snap_logic {

namespace {

const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

struct SnapOrder {
    std::string stamp;
    uint64_t sequence = 0;
};
bool ParseSnapName(const std::string& name, SnapOrder* order) {
    if (name.size() < 24 || name.compare(0, 5, "snap-") || name.compare(name.size() - 4, 4, ".png"))
        return false;
    for (size_t i = 5; i < 20; ++i) {
        if (i == 13) {
            if (name[i] != '-') return false;
        } else if (name[i] < '0' || name[i] > '9')
            return false;
    }
    order->stamp = name.substr(5, 15);
    order->sequence = 0;
    if (name.size() == 24) return true;
    if (name[20] != '-' || name.size() == 25) return false;
    const char* first = name.data() + 21;
    const char* last = name.data() + name.size() - 4;
    auto result = std::from_chars(first, last, order->sequence);
    return result.ec == std::errc() && result.ptr == last;
}

}  // namespace

std::string PickSnapName(const std::vector<std::string>& existingNames, const std::string& stamp) {
    std::set<std::string> existing(existingNames.begin(), existingNames.end());
    std::string candidate = "snap-" + stamp + ".png";
    if (existing.find(candidate) == existing.end()) {
        return candidate;
    }
    int suffix = 1;
    while (true) {
        std::string next = "snap-" + stamp + "-" + std::to_string(suffix++) + ".png";
        if (existing.find(next) == existing.end()) {
            return next;
        }
    }
}

std::vector<std::string> FilesToPrune(const std::vector<std::string>& existingNames, int keepCount) {
    int limit = keepCount > 0 ? keepCount : 20;
    std::vector<std::string> snapFiles;
    for (const auto& name : existingNames) {
        SnapOrder order;
        if (ParseSnapName(name, &order)) {
            snapFiles.push_back(name);
        }
    }

    std::sort(snapFiles.begin(), snapFiles.end(), [](const auto& a, const auto& b) {
        SnapOrder left, right;
        ParseSnapName(a, &left);
        ParseSnapName(b, &right);
        if (left.stamp != right.stamp) return left.stamp < right.stamp;
        if (left.sequence != right.sequence) return left.sequence < right.sequence;
        return a < b;
    });

    std::vector<std::string> toPrune;
    if (static_cast<int>(snapFiles.size()) > limit) {
        int countToDelete = static_cast<int>(snapFiles.size()) - limit;
        for (int i = 0; i < countToDelete; ++i) {
            toPrune.push_back(snapFiles[i]);
        }
    }
    return toPrune;
}

std::string Base64Encode(const uint8_t* data, size_t len) {
    std::string out;
    if (!data || len == 0) return out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i < len) {
        size_t rem = len - i;
        uint32_t b0 = data[i++];
        uint32_t b1 = (rem > 1) ? data[i++] : 0;
        uint32_t b2 = (rem > 2) ? data[i++] : 0;

        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(kBase64Chars[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Chars[(triple >> 12) & 0x3F]);
        out.push_back((rem > 1) ? kBase64Chars[(triple >> 6) & 0x3F] : '=');
        out.push_back((rem > 2) ? kBase64Chars[triple & 0x3F] : '=');
    }
    return out;
}

}  // namespace snap_logic
}  // namespace cronq
