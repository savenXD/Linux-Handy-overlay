#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace cronq {
namespace ghost_logic {

struct Timing {
    bool human = true;
    int speed = 0;  // 0: Slow (300ms), 1: Moderate (115ms), 2: Fast (85ms)
    bool safe = false;
    int letterMinMs = 10;
    int letterMaxMs = 600;
    int linesMin = 3;
    int linesMax = 6;
    int pauseMinMs = 2000;
    int pauseMaxMs = 7000;
    bool preserveLayout = true;
};

Timing DefaultTiming();
Timing ValidateTiming(Timing t);

int SpeedDelayMs(int speed, bool human);
int SpeedWpm(int speed, bool human);

std::string NormalizeText(const std::string& in);

// UTF-8 code point conversion helpers
std::vector<uint32_t> Utf8ToCodepoints(const std::string& utf8);
std::string CodepointToUtf8(uint32_t cp);

// Seedable, deterministic pseudo-random number generator for testing and runtime.
class Rng {
public:
    explicit Rng(uint64_t seed = 0);
    void Seed(uint64_t seed);

    // Inclusive integer range [lo, hi]
    int Range(int lo, int hi);

    // Floating range [0.0, 1.0)
    double Unit();

    // Floating range [lo, hi)
    double RangeDouble(double lo, double hi);

private:
    uint64_t state_;
};

double RandLogNormal(double median, double sigma, Rng& rng);

struct PlanItem {
    uint32_t cp = 0;
    bool count = true;
};

struct KeyInterval {
    int totalMs = 0;
    int leadMs = 0;
    int dwellMs = 0;
    int restMs = 0;
    int postRestExtraMs = 0;  // chunk pause or typo hesitation
};

class Typist {
public:
    Typist(const std::string& normalizedText, Timing timing, uint64_t seed = 0);

    bool HasNext() const;
    size_t TotalChars() const { return totalChars_; }
    size_t TypedChars() const { return typedChars_; }
    size_t LinesTyped() const { return linesTyped_; }
    const Timing& GetTiming() const { return timing_; }

    // Obtains next plan item (character to type and whether it counts toward typedChars).
    PlanItem Next();

    // Computes timing for a character.
    KeyInterval ComputeInterval(uint32_t cp, bool needsMod, bool needsUnicode);

    // Records that a key was typed (advances counts, updates lines).
    void OnCharTyped(uint32_t cp, bool count);

    // Pacing and ETA calculations
    double ModelMs(size_t fromIdx, size_t toIdx) const;
    int EtaRemainingMs(int64_t elapsedMs) const;

private:
    std::vector<uint32_t> codePoints_;
    size_t cursor_ = 0;
    size_t totalChars_ = 0;
    size_t typedChars_ = 0;
    size_t linesTyped_ = 0;
    int nextPauseLines_ = 4;

    Timing timing_;
    Rng rng_;

    std::deque<PlanItem> plan_;
    int sinceTypo_ = 100;
    uint32_t lastChar_ = 0;
    double drift_ = 1.0;
    int burstLeft_ = 0;
};

// Formatting helpers
std::string FormatEta(int etaMs);
std::string FormatStatus(size_t typed, size_t total);
std::string FormatCountdown(int seconds);

// Standard toasts
extern const char* kToastFocusChanged;
extern const char* kToastUnsupportedChar;
extern const char* kToastTypingCancelled;
extern const char* kToastNothingToType;
extern const char* kToastNoFocusedApp;
extern const char* kToastGhostInitFailed;
extern const char* kToastTargetElevated;
extern const char* kToastSettingsSaveFailed;

}  // namespace ghost_logic
}  // namespace cronq
