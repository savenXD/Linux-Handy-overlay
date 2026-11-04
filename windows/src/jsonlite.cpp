#include "jsonlite.h"

#include <charconv>
#include <cstdint>
#include <utility>
namespace cronq {
namespace json {
namespace {
class Parser {
    const std::string& s;
    size_t i = 0;
    void Ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    }
    bool Take(char c) {
        Ws();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    bool Hex(uint32_t& n) {
        n = 0;
        for (int k = 0; k < 4; ++k) {
            if (i >= s.size()) return false;
            char c = s[i++];
            int v = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                           : -1;
            if (v < 0) return false;
            n = n * 16 + v;
        }
        return true;
    }
    void Utf8(std::string& out, uint32_t n) {
        if (n < 128)
            out += char(n);
        else if (n < 2048) {
            out += char(0xc0 | (n >> 6));
            out += char(0x80 | (n & 63));
        } else if (n < 65536) {
            out += char(0xe0 | (n >> 12));
            out += char(0x80 | ((n >> 6) & 63));
            out += char(0x80 | (n & 63));
        } else {
            out += char(0xf0 | (n >> 18));
            out += char(0x80 | ((n >> 12) & 63));
            out += char(0x80 | ((n >> 6) & 63));
            out += char(0x80 | (n & 63));
        }
    }
    bool String(std::string& out) {
        if (!Take('"')) return false;
        while (i < s.size()) {
            unsigned char c = s[i++];
            if (c == '"') return true;
            if (c < 32) return false;
            if (c != '\\') {
                out += char(c);
                continue;
            }
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case '"':
                case '\\':
                case '/':
                    out += e;
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u': {
                    uint32_t n;
                    if (!Hex(n)) return false;
                    if (n >= 0xd800 && n <= 0xdbff) {
                        if (i + 2 > s.size() || s[i++] != '\\' || s[i++] != 'u') return false;
                        uint32_t low;
                        if (!Hex(low) || low < 0xdc00 || low > 0xdfff) return false;
                        n = 0x10000 + ((n - 0xd800) << 10) + (low - 0xdc00);
                    } else if (n >= 0xdc00 && n <= 0xdfff)
                        return false;
                    Utf8(out, n);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    }
    bool Digit() const { return i < s.size() && s[i] >= '0' && s[i] <= '9'; }
    bool Value(std::string& out) {
        Ws();
        if (i == s.size()) return false;
        if (s[i] == '"') return String(out);
        for (auto p : {"true", "false", "null"}) {
            std::string v = p;
            if (s.compare(i, v.size(), v) == 0) {
                out = v;
                i += v.size();
                return true;
            }
        }
        size_t start = i;
        if (s[i] == '-') ++i;
        if (!Digit()) return false;
        if (s[i] == '0')
            ++i;
        else
            while (Digit()) ++i;
        if (i < s.size() && s[i] == '.') {
            ++i;
            if (!Digit()) return false;
            while (Digit()) ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (!Digit()) return false;
            while (Digit()) ++i;
        }
        out = s.substr(start, i - start);
        return true;
    }

   public:
    explicit Parser(const std::string& text) : s(text) {}
    bool Parse(std::map<std::string, std::string>& out) {
        if (!Take('{')) return false;
        if (!Take('}')) {
            do {
                std::string key, value;
                if (!String(key) || !Take(':') || !Value(value)) return false;
                // DECISION: reject duplicate keys rather than dispatch an ambiguous command.
                if (!out.emplace(key, value).second) return false;
            } while (Take(','));
            if (!Take('}')) return false;
        }
        Ws();
        return i == s.size();
    }
};
}  // namespace
bool ParseFlat(const std::string& text, std::map<std::string, std::string>* out) {
    if (!out || text.size() > 16u * 1024u * 1024u) return false;
    std::map<std::string, std::string> result;
    if (!Parser(text).Parse(result)) return false;
    *out = std::move(result);
    return true;
}
bool ParseInt(const std::string& text, int minimum, int maximum, int* out) {
    if (!out || text.empty()) return false;
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() || value < minimum ||
        value > maximum)
        return false;
    *out = value;
    return true;
}
std::string Escape(const std::string& s) {
    std::string out;
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : s) switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 32) {
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else
                    out += char(c);
        }
    return out;
}
}  // namespace json
}  // namespace cronq
