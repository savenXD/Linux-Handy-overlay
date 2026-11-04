#include "key_translate.h"

#include <cstdio>

using namespace cronq::key_translate;

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);        \
            g_failures++;                                              \
        }                                                              \
    } while (0)

int main() {
    // 1. Letters
    KeyInfo aLower = TranslateVk('A', false);
    CHECK(aLower.key == "a" && aLower.code == "KeyA" && aLower.printable);

    KeyInfo aUpper = TranslateVk('A', true);
    CHECK(aUpper.key == "A" && aUpper.code == "KeyA" && aUpper.printable);

    // 2. Digits
    KeyInfo dig1 = TranslateVk('1', false);
    CHECK(dig1.key == "1" && dig1.code == "Digit1" && dig1.printable);

    // 3. Special keys
    KeyInfo bs = TranslateVk(0x08, false);  // VK_BACK
    CHECK(bs.key == "Backspace" && bs.code == "Backspace" && !bs.printable);

    KeyInfo ret = TranslateVk(0x0D, false);  // VK_RETURN
    CHECK(ret.key == "Enter" && ret.code == "Enter" && !ret.printable);

    KeyInfo tab = TranslateVk(0x09, false);  // VK_TAB
    CHECK(tab.key == "Tab" && tab.code == "Tab" && !tab.printable);

    KeyInfo sp = TranslateVk(0x20, false);  // VK_SPACE
    CHECK(sp.key == " " && sp.code == "Space" && sp.printable);

    KeyInfo left = TranslateVk(0x25, false);  // VK_LEFT
    CHECK(left.key == "ArrowLeft" && left.code == "ArrowLeft" && !left.printable);

    // 4. Function keys
    KeyInfo f1 = TranslateVk(0x70, false);
    CHECK(f1.key == "F1" && f1.code == "F1");

    KeyInfo f12 = TranslateVk(0x7B, false);
    CHECK(f12.key == "F12" && f12.code == "F12");

    // 5. Modifier detection
    CHECK(IsModifierOnly(0x10));  // VK_SHIFT
    CHECK(IsModifierOnly(0x11));  // VK_CONTROL
    CHECK(IsModifierOnly(0x12));  // VK_MENU (Alt)
    CHECK(IsModifierOnly(0x5B));  // VK_LWIN
    CHECK(IsModifierOnly(0x14));  // VK_CAPITAL (CapsLock)
    CHECK(!IsModifierOnly('A'));
    CHECK(!IsModifierOnly(0x0D));

    if (g_failures == 0) {
        std::printf("key_translate_test: all checks passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
