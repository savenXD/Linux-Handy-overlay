#pragma once

#include <string>

namespace cronq {
namespace key_translate {

struct KeyInfo {
    std::string key;
    std::string code;
    int windowsVirtualKeyCode = 0;
    bool printable = false;
    bool isModifier = false;
};

// Translates a Win32 virtual-key code (VK_*) into Chrome DevTools Protocol key information.
KeyInfo TranslateVk(int vk, bool shiftDown = false);

// Returns true if the VK code is a standalone modifier key (Shift, Ctrl, Alt, Win, CapsLock).
bool IsModifierOnly(int vk);

}  // namespace key_translate
}  // namespace cronq
