#include "ghost_logic.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace cronq::ghost_logic;

static int g_failures = 0;
#define CHECK(cond)                                             \
    do {                                                        \
        if (!(cond)) {                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
            g_failures++;                                       \
        }                                                       \
    } while (0)

int main() {
    // 1. Timing validation & speeds
    Timing def = DefaultTiming();
    CHECK(def.human == true && def.speed == 0 && def.safe == false);
    CHECK(SpeedDelayMs(0, true) == 300);
    CHECK(SpeedDelayMs(1, true) == 115);
    CHECK(SpeedDelayMs(2, true) == 85);
    CHECK(SpeedDelayMs(0, false) == 0);
    CHECK(SpeedWpm(0, true) == 40);
    CHECK(SpeedWpm(1, true) == 104);
    CHECK(SpeedWpm(2, true) == 141);

    Timing bad;
    bad.speed = 99;
    bad.safe = false;
    bad = ValidateTiming(bad);
    CHECK(bad.speed == 0);
    CHECK(bad.letterMinMs == 150);
    CHECK(bad.letterMaxMs == 600);
    CHECK(bad.linesMin == 3 && bad.linesMax == 6);

    // 2. NormalizeText
    std::string norm1 = NormalizeText("line1\r\nline2\rline3\n\n\n");
    CHECK(norm1 == "line1\nline2\nline3");

    std::string norm2 = NormalizeText("single line\r\n");
    CHECK(norm2 == "single line");

    std::string norm3 = NormalizeText("");
    CHECK(norm3.empty());

    // 3. UTF-8 code point conversion
    std::string sample = "Hello, 世界! 🚀";
    auto cps = Utf8ToCodepoints(sample);
    CHECK(cps.size() == 12);    // 'H','e','l','l','o',',',' ','世','界','!',' ','🚀'
    CHECK(cps[7] == 0x4E16);    // 世
    CHECK(cps[8] == 0x754C);    // 界
    CHECK(cps[11] == 0x1F680);  // 🚀
    std::string back;
    for (uint32_t cp : cps) {
        back += CodepointToUtf8(cp);
    }
    CHECK(back == sample);

    // 4. Rng deterministic
    Rng r1(12345);
    Rng r2(12345);
    for (int i = 0; i < 50; ++i) {
        CHECK(r1.Range(10, 50) == r2.Range(10, 50));
        CHECK(r1.Unit() == r2.Unit());
    }

    // 5. Typist instant mode (human = false)
    Timing instantTiming;
    instantTiming.human = false;
    Typist tInstant("quick test", instantTiming, 42);
    CHECK(tInstant.TotalChars() == 10);
    std::string typedText;
    while (tInstant.HasNext()) {
        PlanItem it = tInstant.Next();
        KeyInterval ki = tInstant.ComputeInterval(it.cp, false, false);
        CHECK(ki.dwellMs == 8);
        CHECK(ki.restMs == 1);
        tInstant.OnCharTyped(it.cp, it.count);
        typedText += CodepointToUtf8(it.cp);
    }
    CHECK(typedText == "quick test");
    CHECK(tInstant.TypedChars() == 10);

    // 6. Typist extra-safe mode with typos and corrections
    Timing safeTiming;
    safeTiming.human = true;
    safeTiming.safe = true;
    safeTiming.speed = 1;
    // Use a multi-word input to trigger typo path
    std::string safeInput = "the quick brown fox jumps over the lazy dog repeatedly";
    Typist tSafe(safeInput, safeTiming, 100);

    std::string simulationBuffer;
    while (tSafe.HasNext()) {
        PlanItem it = tSafe.Next();
        tSafe.ComputeInterval(it.cp, false, false);
        tSafe.OnCharTyped(it.cp, it.count);
        if (it.cp == '\b') {
            if (!simulationBuffer.empty()) simulationBuffer.pop_back();
        } else {
            simulationBuffer += CodepointToUtf8(it.cp);
        }
    }
    // Final simulation buffer must EXACTLY match safeInput despite typos!
    CHECK(simulationBuffer == safeInput);

    CHECK(Utf8ToCodepoints("\xC0\xAF").empty());          // overlong ASCII
    CHECK(Utf8ToCodepoints("\xED\xA0\x80").empty());      // surrogate
    CHECK(Utf8ToCodepoints("\xF4\x90\x80\x80").empty());  // beyond Unicode

    // 7. Format helpers
    CHECK(FormatEta(0) == "");
    CHECK(FormatEta(25000) == "est: 25s");
    CHECK(FormatEta(125000) == "est: 2m 05s");
    CHECK(FormatEta(3665000) == "est: 1h 01m");
    CHECK(FormatStatus(5, 20) == "Typing 5 / 20");
    CHECK(FormatCountdown(3) == "Starting in 3… click the target");

    if (g_failures == 0) {
        std::printf("ghost_logic_test: all checks passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
