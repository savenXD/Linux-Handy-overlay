// Restart / recovery pacing shared by the supervisor and the WebView2 recovery path (no Win32, unit-testable).
#pragma once

#include <algorithm>
#include <cstdint>

namespace cronq {
namespace recovery_logic {

// Daemon exit code meaning "a restart cannot help" (for example capture protection is unavailable on
// this Windows version). The supervisor stops instead of looping.
constexpr unsigned kExitNoRestart = 3;
constexpr unsigned kStatusControlCExit = 0xC000013A;

inline bool ShouldRestart(unsigned exitCode) {
    return exitCode != 0 && exitCode != kStatusControlCExit && exitCode != kExitNoRestart;
}

// Failures closer together than `stableMs` escalate: first, then step, 2*step, 4*step ... up to max.
// A failure after a quiet period starts over at `first`.
class Backoff {
   public:
    Backoff(int64_t firstMs, int64_t stepMs, int64_t maxMs, int64_t stableMs)
        : first_(firstMs), step_(stepMs), max_(maxMs), stable_(stableMs) {}

    int64_t Next(int64_t nowMs) {
        if (last_ >= 0 && nowMs - last_ < stable_)
            n_ = std::min(n_ + 1, 30);
        else
            n_ = 0;
        last_ = nowMs;
        if (n_ == 0) return first_;
        return std::min(step_ << std::min(n_ - 1, 20), max_);
    }
    int Streak() const { return n_; }

   private:
    int64_t first_, step_, max_, stable_;
    int64_t last_ = -1;
    int n_ = 0;
};

}  // namespace recovery_logic
}  // namespace cronq
