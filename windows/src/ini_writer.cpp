#include "ini_writer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <sstream>
#include <vector>

namespace cronq {
namespace ini_writer {

namespace {

bool EqNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string Trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() &&
           (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        start++;
    }
    size_t end = s.size();
    while (end > start &&
           (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        end--;
    }
    return s.substr(start, end - start);
}

// Splits text into lines preserving line endings in each line
struct LineInfo {
    size_t start;
    size_t length;
    std::string text;  // without line ending
    std::string eol;   // "\r\n" or "\n" or ""
};

std::vector<LineInfo> SplitLines(const std::string& text) {
    std::vector<LineInfo> lines;
    size_t i = 0;
    while (i < text.size()) {
        size_t start = i;
        while (i < text.size() && text[i] != '\n' && text[i] != '\r') {
            i++;
        }
        size_t textEnd = i;
        std::string eol;
        if (i < text.size() && text[i] == '\r') {
            eol += '\r';
            i++;
        }
        if (i < text.size() && text[i] == '\n') {
            eol += '\n';
            i++;
        }
        LineInfo li;
        li.start = start;
        li.length = i - start;
        li.text = text.substr(start, textEnd - start);
        if (start == 0 && li.text.compare(0, 3, "\xEF\xBB\xBF") == 0) li.text.erase(0, 3);
        li.eol = eol;
        lines.push_back(li);
    }
    return lines;
}

}  // namespace

bool SetIniInt(std::string& text, const char* section, const char* key, int value) {
    std::string eol = (text.find("\r\n") != std::string::npos) ? "\r\n" : "\n";
    std::string valStr = std::to_string(value);
    std::string targetSec = section ? section : "";
    std::string targetKey = key ? key : "";

    auto lines = SplitLines(text);
    bool inTargetSec = false;
    size_t insertBeforeLineIdx = static_cast<size_t>(-1);

    for (size_t idx = 0; idx < lines.size(); ++idx) {
        std::string trimmed = Trim(lines[idx].text);
        if (trimmed.empty()) continue;

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            std::string secName = Trim(trimmed.substr(1, trimmed.size() - 2));
            if (inTargetSec) {
                // Next section encountered before finding key
                insertBeforeLineIdx = idx;
                break;
            }
            if (EqNoCase(secName, targetSec)) {
                inTargetSec = true;
            }
            continue;
        }

        if (inTargetSec) {
            if (trimmed.front() == '#' || trimmed.front() == ';') {
                continue;  // comment line
            }
            size_t eqPos = lines[idx].text.find('=');
            if (eqPos == std::string::npos) {
                eqPos = lines[idx].text.find(':');
            }
            if (eqPos != std::string::npos) {
                std::string k = Trim(lines[idx].text.substr(0, eqPos));
                if (EqNoCase(k, targetKey)) {
                    // Replace in-place
                    // Keep key and separator, check for trailing comments
                    const auto& original = lines[idx].text;
                    size_t valueStart = eqPos + 1;
                    while (valueStart < original.size() &&
                           (original[valueStart] == ' ' || original[valueStart] == '\t'))
                        ++valueStart;
                    size_t valueEnd = original.find_first_of(";#", valueStart);
                    if (valueEnd == std::string::npos) valueEnd = original.size();
                    while (valueEnd > valueStart &&
                           (original[valueEnd - 1] == ' ' || original[valueEnd - 1] == '\t'))
                        --valueEnd;
                    std::string newLine =
                        original.substr(0, valueStart) + valStr + original.substr(valueEnd) + lines[idx].eol;
                    text.replace(lines[idx].start, lines[idx].length, newLine);
                    return true;
                }
            }
        }
    }

    if (inTargetSec) {
        // Section exists, but key not found. Insert before insertBeforeLineIdx or at end.
        std::string newLine = targetKey + " = " + valStr + eol;
        if (insertBeforeLineIdx != static_cast<size_t>(-1)) {
            size_t pos = lines[insertBeforeLineIdx].start;
            text.insert(pos, newLine);
        } else {
            // Append at end of file. Ensure previous line had eol
            if (!text.empty() && text.back() != '\n' && text.back() != '\r') {
                text += eol;
            }
            text += newLine;
        }
        return true;
    }

    // Section does not exist. Append [section] and key.
    if (!text.empty() && text.back() != '\n' && text.back() != '\r') {
        text += eol;
    }
    if (!text.empty()) {
        text += eol;
    }
    text += "[" + targetSec + "]" + eol;
    text += targetKey + " = " + valStr + eol;
    return true;
}

int GetIniInt(const std::string& text, const char* section, const char* key, int defaultValue) {
    if (!section || !key) return defaultValue;
    auto lines = SplitLines(text);
    bool inTargetSec = false;

    for (const auto& line : lines) {
        std::string trimmed = Trim(line.text);
        if (trimmed.empty()) continue;

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            std::string secName = Trim(trimmed.substr(1, trimmed.size() - 2));
            if (inTargetSec) {
                return defaultValue;  // left target section
            }
            if (EqNoCase(secName, section)) {
                inTargetSec = true;
            }
            continue;
        }

        if (inTargetSec) {
            if (trimmed.front() == '#' || trimmed.front() == ';') continue;
            size_t eqPos = line.text.find('=');
            if (eqPos == std::string::npos) eqPos = line.text.find(':');
            if (eqPos != std::string::npos) {
                std::string k = Trim(line.text.substr(0, eqPos));
                if (EqNoCase(k, key)) {
                    std::string v = Trim(line.text.substr(eqPos + 1));
                    size_t commPos = v.find_first_of(";#");
                    if (commPos != std::string::npos) {
                        v = Trim(v.substr(0, commPos));
                    }
                    if (v.empty()) return defaultValue;
                    char* end = nullptr;
                    errno = 0;
                    long val = std::strtol(v.c_str(), &end, 10);
                    if (end && end != v.c_str() && *end == 0 && errno != ERANGE && val >= INT_MIN &&
                        val <= INT_MAX) {
                        return static_cast<int>(val);
                    }
                    return defaultValue;
                }
            }
        }
    }
    return defaultValue;
}

}  // namespace ini_writer
}  // namespace cronq
