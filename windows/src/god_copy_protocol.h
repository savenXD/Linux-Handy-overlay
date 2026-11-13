#pragma once
#include <cstdint>
#include <string>

namespace cronq::god_copy {
constexpr uint32_t kMagic = 0x47435031; // GCP1
constexpr uint32_t kMaxText = 262144;
enum class Result : uint32_t { Pending, Ok, Unsupported, NoSelection, Password, Changed, TooLarge, Failed };
struct Packet {
    uint32_t magic;
    Result result;
    uint64_t window;
    uint64_t focus;
    uint32_t process;
    uint32_t length;
    uint32_t backend; // 1 = standard EDIT; 2 = UI Automation
    wchar_t text[kMaxText + 1];
};
// Reject malformed/truncated worker output instead of publishing partial selections.
inline bool Valid(const Packet& p) {
    if (p.magic != kMagic || p.result != Result::Ok || !p.length || p.length > kMaxText ||
        p.text[p.length] != 0) return false;
    for (uint32_t i = 0; i < p.length; ++i) {
        uint32_t c = p.text[i];
        if (!c) return false;
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (++i >= p.length || p.text[i] < 0xDC00 || p.text[i] > 0xDFFF) return false;
        } else if (c >= 0xDC00 && c <= 0xDFFF) return false;
    }
    return true;
}
}
