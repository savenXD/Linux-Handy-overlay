#include "key_translate.h"

#include <cctype>

namespace cronq {
namespace key_translate {

bool IsModifierOnly(int vk) {
    switch (vk) {
        case 0x10:  // VK_SHIFT
        case 0xA0:  // VK_LSHIFT
        case 0xA1:  // VK_RSHIFT
        case 0x11:  // VK_CONTROL
        case 0xA2:  // VK_LCONTROL
        case 0xA3:  // VK_RCONTROL
        case 0x12:  // VK_MENU (Alt)
        case 0xA4:  // VK_LMENU
        case 0xA5:  // VK_RMENU
        case 0x5B:  // VK_LWIN
        case 0x5C:  // VK_RWIN
        case 0x14:  // VK_CAPITAL (CapsLock)
            return true;
        default:
            return false;
    }
}

KeyInfo TranslateVk(int vk, bool shiftDown) {
    KeyInfo info;
    info.windowsVirtualKeyCode = vk;
    info.printable = false;
    info.isModifier = IsModifierOnly(vk);

    if (info.isModifier) {
        switch (vk) {
            case 0x10: case 0xA0: info.key = "Shift"; info.code = "ShiftLeft"; break;
            case 0xA1: info.key = "Shift"; info.code = "ShiftRight"; break;
            case 0x11: case 0xA2: info.key = "Control"; info.code = "ControlLeft"; break;
            case 0xA3: info.key = "Control"; info.code = "ControlRight"; break;
            case 0x12: case 0xA4: info.key = "Alt"; info.code = "AltLeft"; break;
            case 0xA5: info.key = "Alt"; info.code = "AltRight"; break;
            case 0x5B: info.key = "Meta"; info.code = "MetaLeft"; break;
            case 0x5C: info.key = "Meta"; info.code = "MetaRight"; break;
            case 0x14: info.key = "CapsLock"; info.code = "CapsLock"; break;
            default: info.key = "Unidentified"; info.code = ""; break;
        }
        return info;
    }

    // Letters 'A' - 'Z'
    if (vk >= 'A' && vk <= 'Z') {
        char ch = static_cast<char>(vk);
        info.code = std::string("Key") + ch;
        info.key = shiftDown ? std::string(1, ch) : std::string(1, static_cast<char>(std::tolower(ch)));
        info.printable = true;
        return info;
    }

    // Digits '0' - '9'
    if (vk >= '0' && vk <= '9') {
        char ch = static_cast<char>(vk);
        info.code = std::string("Digit") + ch;
        info.key = std::string(1, ch);
        info.printable = true;
        return info;
    }

    // Function keys F1 - F12
    if (vk >= 0x70 && vk <= 0x7B) {
        int num = vk - 0x70 + 1;
        info.key = "F" + std::to_string(num);
        info.code = "F" + std::to_string(num);
        return info;
    }

    // Numpad 0 - 9
    if (vk >= 0x60 && vk <= 0x69) {
        int num = vk - 0x60;
        info.key = std::to_string(num);
        info.code = "Numpad" + std::to_string(num);
        info.printable = true;
        return info;
    }

    switch (vk) {
        case 0x08:  // VK_BACK
            info.key = "Backspace";
            info.code = "Backspace";
            break;
        case 0x09:  // VK_TAB
            info.key = "Tab";
            info.code = "Tab";
            break;
        case 0x0D:  // VK_RETURN
            info.key = "Enter";
            info.code = "Enter";
            break;
        case 0x1B:  // VK_ESCAPE
            info.key = "Escape";
            info.code = "Escape";
            break;
        case 0x20:  // VK_SPACE
            info.key = " ";
            info.code = "Space";
            info.printable = true;
            break;
        case 0x21:  // VK_PRIOR (PageUp)
            info.key = "PageUp";
            info.code = "PageUp";
            break;
        case 0x22:  // VK_NEXT (PageDown)
            info.key = "PageDown";
            info.code = "PageDown";
            break;
        case 0x23:  // VK_END
            info.key = "End";
            info.code = "End";
            break;
        case 0x24:  // VK_HOME
            info.key = "Home";
            info.code = "Home";
            break;
        case 0x25:  // VK_LEFT
            info.key = "ArrowLeft";
            info.code = "ArrowLeft";
            break;
        case 0x26:  // VK_UP
            info.key = "ArrowUp";
            info.code = "ArrowUp";
            break;
        case 0x27:  // VK_RIGHT
            info.key = "ArrowRight";
            info.code = "ArrowRight";
            break;
        case 0x28:  // VK_DOWN
            info.key = "ArrowDown";
            info.code = "ArrowDown";
            break;
        case 0x2D:  // VK_INSERT
            info.key = "Insert";
            info.code = "Insert";
            break;
        case 0x2E:  // VK_DELETE
            info.key = "Delete";
            info.code = "Delete";
            break;
        case 0xBA:  // VK_OEM_1 (;:)
            info.key = shiftDown ? ":" : ";";
            info.code = "Semicolon";
            info.printable = true;
            break;
        case 0xBB:  // VK_OEM_PLUS (=+)
            info.key = shiftDown ? "+" : "=";
            info.code = "Equal";
            info.printable = true;
            break;
        case 0xBC:  // VK_OEM_COMMA (,<)
            info.key = shiftDown ? "<" : ",";
            info.code = "Comma";
            info.printable = true;
            break;
        case 0xBD:  // VK_OEM_MINUS (-_)
            info.key = shiftDown ? "_" : "-";
            info.code = "Minus";
            info.printable = true;
            break;
        case 0xBE:  // VK_OEM_PERIOD (.>)
            info.key = shiftDown ? ">" : ".";
            info.code = "Period";
            info.printable = true;
            break;
        case 0xBF:  // VK_OEM_2 (/?)
            info.key = shiftDown ? "?" : "/";
            info.code = "Slash";
            info.printable = true;
            break;
        case 0xC0:  // VK_OEM_3 (`~)
            info.key = shiftDown ? "~" : "`";
            info.code = "Backquote";
            info.printable = true;
            break;
        case 0xDB:  // VK_OEM_4 ([{)
            info.key = shiftDown ? "{" : "[";
            info.code = "BracketLeft";
            info.printable = true;
            break;
        case 0xDC:  // VK_OEM_5 (\|)
            info.key = shiftDown ? "|" : "\\";
            info.code = "Backslash";
            info.printable = true;
            break;
        case 0xDD:  // VK_OEM_6 (]})
            info.key = shiftDown ? "}" : "]";
            info.code = "BracketRight";
            info.printable = true;
            break;
        case 0xDE:  // VK_OEM_7 ('")
            info.key = shiftDown ? "\"" : "'";
            info.code = "Quote";
            info.printable = true;
            break;
        default:
            info.key = "Unidentified";
            info.code = "";
            break;
    }
    return info;
}

}  // namespace key_translate
}  // namespace cronq
