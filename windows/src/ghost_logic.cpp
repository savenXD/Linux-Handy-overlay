#include "ghost_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace cronq {
namespace ghost_logic {

namespace {

const int kModeDelayMs[3] = {300, 115, 85};

const char* kTypoNear[26] = {"qsz",  "vgn", "xdv", "sefc", "wrd", "drgv", "fthb", "gyjn", "uok",
                             "hukm", "jil", "kop", "nj",   "bhm", "ipl",  "ol",   "wa",   "etf",
                             "awdx", "ryg", "yij", "cfb",  "qes", "zsc",  "tuh",  "asx"};

}  // namespace

const char* kToastFocusChanged = "Stopped: focus changed";
const char* kToastUnsupportedChar = "Stopped: unsupported character";
const char* kToastTypingCancelled = "Typing cancelled";
const char* kToastNothingToType = "Nothing to type";
const char* kToastNoFocusedApp = "No focused app";
const char* kToastGhostInitFailed = "Ghost init failed";
const char* kToastTargetElevated = "Target runs as administrator";
const char* kToastSettingsSaveFailed = "Timing applied; could not save settings";

Timing DefaultTiming() {
    Timing t;
    t.human = true;
    t.speed = 0;
    t.safe = false;
    return ValidateTiming(t);
}

Timing ValidateTiming(Timing t) {
    if (t.speed < 0 || t.speed > 2) t.speed = 0;
    int m = SpeedDelayMs(t.speed, t.human);
    t.letterMinMs = std::max(10, m / 2);
    t.letterMaxMs = std::max(10, m * 2);
    t.linesMin = 3;
    t.linesMax = 6;
    t.pauseMinMs = 2000;
    t.pauseMaxMs = 7000;
    t.preserveLayout = true;
    t.safe = t.safe ? true : false;
    return t;
}

int SpeedDelayMs(int speed, bool human) {
    if (!human) return 0;
    int s = std::max(0, std::min(2, speed));
    return kModeDelayMs[s];
}

int SpeedWpm(int speed, bool human) {
    int d = SpeedDelayMs(speed, human);
    return d ? static_cast<int>(12000.0 / d + 0.5) : 0;
}

std::string NormalizeText(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '\r') {
            if (i + 1 < in.size() && in[i + 1] == '\n') {
                continue;  // CRLF: skip \r, keep \n
            }
            out.push_back('\n');  // lone \r becomes \n
        } else {
            out.push_back(c);
        }
    }
    // Strip trailing \n so pasted text never sends a final submitting Return
    while (!out.empty() && out.back() == '\n') {
        out.pop_back();
    }
    return out;
}

std::vector<uint32_t> Utf8ToCodepoints(const std::string& utf8) {
    std::vector<uint32_t> cps;
    cps.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        uint8_t b = static_cast<uint8_t>(utf8[i]);
        uint32_t cp = 0;
        size_t len = 0;
        if (b < 0x80) {
            cp = b;
            len = 1;
        } else if ((b & 0xE0) == 0xC0) {
            cp = b & 0x1F;
            len = 2;
        } else if ((b & 0xF0) == 0xE0) {
            cp = b & 0x0F;
            len = 3;
        } else if ((b & 0xF8) == 0xF0) {
            cp = b & 0x07;
            len = 4;
        } else {
            // invalid byte, advance
            i++;
            continue;
        }
        if (i + len > utf8.size()) break;
        bool valid = true;
        for (size_t j = 1; j < len; ++j) {
            uint8_t nb = static_cast<uint8_t>(utf8[i + j]);
            if ((nb & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (nb & 0x3F);
        }
        valid = valid && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF) &&
                (len == 1 || (len == 2 && cp >= 0x80) || (len == 3 && cp >= 0x800) ||
                 (len == 4 && cp >= 0x10000));
        if (valid) {
            cps.push_back(cp);
            i += len;
        } else {
            i++;
        }
    }
    return cps;
}

std::string CodepointToUtf8(uint32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x110000) {
        s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return s;
}

Rng::Rng(uint64_t seed) : state_(seed ? seed : 0x853c49e6748fea9bULL) {}

void Rng::Seed(uint64_t seed) { state_ = seed ? seed : 0x853c49e6748fea9bULL; }

int Rng::Range(int lo, int hi) {
    if (lo >= hi) return lo;
    uint32_t span = static_cast<uint32_t>(hi - lo + 1);
    // SplitMix64 step
    state_ += 0x9e3779b97f4a7c15ULL;
    uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    uint32_t r = static_cast<uint32_t>((z ^ (z >> 31)) & 0xFFFFFFFF);
    return lo + static_cast<int>(r % span);
}

double Rng::Unit() {
    state_ += 0x9e3779b97f4a7c15ULL;
    uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    uint64_t res = z ^ (z >> 31);
    return static_cast<double>(res >> 11) * (1.0 / 9007199254740992.0);
}

double Rng::RangeDouble(double lo, double hi) { return lo + (hi - lo) * Unit(); }

double RandLogNormal(double median, double sigma, Rng& rng) {
    double u1 = std::max(1e-9, rng.Unit());
    double u2 = rng.Unit();
    return median * std::exp(sigma * std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2));
}

Typist::Typist(const std::string& normalizedText, Timing timing, uint64_t seed)
    : timing_(ValidateTiming(timing)), rng_(seed) {
    codePoints_ = Utf8ToCodepoints(normalizedText);
    totalChars_ = codePoints_.size();
    nextPauseLines_ = rng_.Range(timing_.linesMin, timing_.linesMax);
}

bool Typist::HasNext() const { return !plan_.empty() || cursor_ < codePoints_.size(); }

PlanItem Typist::Next() {
    if (!plan_.empty()) {
        PlanItem it = plan_.front();
        plan_.pop_front();
        return it;
    }
    if (cursor_ >= codePoints_.size()) {
        return {0, false};
    }
    uint32_t c = codePoints_[cursor_++];
    sinceTypo_++;

    // Extra-safe typo simulation
    if (timing_.human && timing_.safe && c >= 'a' && c <= 'z' && sinceTypo_ > 6 && rng_.Range(0, 99) < 3) {
        int want = rng_.Range(0, 2);
        std::vector<uint32_t> extras;
        while (static_cast<int>(extras.size()) < want && cursor_ < codePoints_.size()) {
            uint32_t e = codePoints_[cursor_];
            if (e < 'a' || e > 'z') break;
            extras.push_back(e);
            cursor_++;
        }
        for (uint32_t e : extras) plan_.push_back({e, false});
        for (size_t i = 0; i <= extras.size(); ++i) plan_.push_back({'\b', false});
        plan_.push_back({c, true});
        for (uint32_t e : extras) plan_.push_back({e, true});

        sinceTypo_ = 0;
        const char* nearChars = kTypoNear[c - 'a'];
        size_t nearLen = std::strlen(nearChars);
        uint32_t typoChar = static_cast<uint32_t>(nearChars[rng_.Range(0, static_cast<int>(nearLen - 1))]);
        return {typoChar, false};
    }

    return {c, true};
}

KeyInterval Typist::ComputeInterval(uint32_t cp, bool needsMod, bool needsUnicode) {
    KeyInterval ki;
    double m = SpeedDelayMs(timing_.speed, timing_.human);

    if (!timing_.human) {
        ki.totalMs = static_cast<int>(m);
        ki.dwellMs = 8;
        ki.leadMs = needsMod ? 5 : 0;
        if (needsUnicode) ki.leadMs = std::max(ki.leadMs, 35);
        ki.restMs = 1;
        return ki;
    }

    if (timing_.safe) m *= 1.1;
    if (cp == ' ') {
        drift_ = std::max(0.85, std::min(1.2, drift_ + rng_.RangeDouble(-0.06, 0.06)));
    }
    m *= drift_;

    double d = RandLogNormal(m, 0.32, rng_);
    if (cp == ' ')
        d *= 1.2;
    else if (cp == '\n')
        d *= 2.2;
    else if (cp == ',' || cp == '.' || cp == ';' || cp == ':' || cp == '!' || cp == '?')
        d *= 1.5;
    else if (cp >= 'A' && cp <= 'Z')
        d *= 1.15;

    if (cp == lastChar_) d *= 0.85;

    if (timing_.speed >= 1) {
        if (burstLeft_ <= 0 && cp == ' ' && rng_.Range(0, 99) < 10) {
            burstLeft_ = rng_.Range(15, 45);
        }
        if (burstLeft_ > 0) {
            d *= 0.72;
            burstLeft_--;
        }
    }

    d = std::max(std::max(35.0, m * 0.45), std::min(d, m * 4.0));
    if (rng_.Range(0, 99) < (timing_.safe ? 6 : 3)) {
        d += rng_.Range(250, 800);
    }

    if (timing_.safe) {
        int r = rng_.Range(0, 99);
        if (cp == ' ' && r < 7) {
            d += std::max(500.0, std::min(2600.0, RandLogNormal(1000.0, 0.4, rng_)));
        } else if ((cp == '.' || cp == '!' || cp == '?') && r < 25) {
            d += rng_.Range(600, 1800);
        } else if ((cp == ',' || cp == ';' || cp == ':') && r < 12) {
            d += rng_.Range(300, 900);
        } else if (cp == '\n' && r < 30) {
            d += rng_.Range(800, 2500);
        }
    }

    ki.totalMs = static_cast<int>(d);
    double dwellTarget = RandLogNormal(std::min(80.0, ki.totalMs * 0.7), 0.25, rng_);
    ki.dwellMs = static_cast<int>(std::max(35.0, std::min(170.0, dwellTarget)));
    if (ki.dwellMs > ki.totalMs * 7 / 10) {
        ki.dwellMs = std::max(2, ki.totalMs * 7 / 10);
    }

    ki.leadMs = needsMod ? rng_.Range(18, 45) : 0;
    if (needsUnicode) ki.leadMs = std::max(ki.leadMs, 35);

    ki.restMs = std::max(1, ki.totalMs - ki.leadMs - ki.dwellMs);

    // Extra pauses
    if (!plan_.empty() && plan_.front().cp == '\b' && lastChar_ != '\b') {
        ki.postRestExtraMs += rng_.Range(200, 599);
    }
    if (timing_.human && linesTyped_ >= static_cast<size_t>(nextPauseLines_) && HasNext()) {
        ki.postRestExtraMs += rng_.Range(timing_.pauseMinMs, timing_.pauseMaxMs);
        linesTyped_ = 0;
        nextPauseLines_ = rng_.Range(timing_.linesMin, timing_.linesMax);
    }

    return ki;
}

void Typist::OnCharTyped(uint32_t cp, bool count) {
    lastChar_ = cp;
    if (count) {
        typedChars_++;
        if (cp == '\n') linesTyped_++;
    }
}

double Typist::ModelMs(size_t fromIdx, size_t toIdx) const {
    if (fromIdx >= toIdx || toIdx > codePoints_.size()) return 0.0;
    long n = 0, lines = 0;
    for (size_t i = fromIdx; i < toIdx; ++i) {
        n++;
        if (codePoints_[i] == '\n') lines++;
    }
    if (!timing_.human) return n * 1.0;
    double m = SpeedDelayMs(timing_.speed, true);
    double per = m * 1.15 + (timing_.safe ? m * 0.1 + 35.0 : 0.0);
    return n * per + lines * 1000.0;
}

int Typist::EtaRemainingMs(int64_t elapsedMs) const {
    if (!HasNext()) return 0;
    double left = ModelMs(cursor_, codePoints_.size());
    if (typedChars_ >= 25) {
        double doneModel = ModelMs(0, cursor_);
        if (doneModel > 0.0) {
            double ratio = std::max(0.6, std::min(1.8, static_cast<double>(elapsedMs) / doneModel));
            double w = std::min(1.0, static_cast<double>(typedChars_) / 100.0);
            left *= 1.0 + (ratio - 1.0) * w;
        }
    }
    return static_cast<int>(left);
}

std::string FormatEta(int etaMs) {
    if (etaMs <= 0) return "";
    int totalSec = (etaMs + 999) / 1000;
    char buf[64];
    if (totalSec < 60) {
        snprintf(buf, sizeof(buf), "est: %ds", std::max(1, totalSec));
    } else if (totalSec < 3600) {
        int m = totalSec / 60;
        int s = totalSec % 60;
        snprintf(buf, sizeof(buf), "est: %dm %02ds", m, s);
    } else {
        int h = totalSec / 3600;
        int m = (totalSec % 3600) / 60;
        snprintf(buf, sizeof(buf), "est: %dh %02dm", h, m);
    }
    return buf;
}

std::string FormatStatus(size_t typed, size_t total) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Typing %zu / %zu", typed, total);
    return buf;
}

std::string FormatCountdown(int seconds) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Starting in %d\xe2\x80\xa6 click the target", seconds);
    return buf;
}

}  // namespace ghost_logic
}  // namespace cronq
