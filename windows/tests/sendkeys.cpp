// Dev tool: sends a key chord with SendInput, e.g.  sendkeys alt+x   or   sendkeys alt+a alt+a alt+s
// Used to exercise the global hotkey hook without touching the keyboard.
#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static void Key(WORD vk, bool up) {
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof(in));
}

static WORD VkFor(const std::string& name) {
    if (name == "alt") return VK_MENU;
    if (name == "ctrl") return VK_CONTROL;
    if (name == "shift") return VK_SHIFT;
    if (name == "left") return VK_LEFT;
    if (name == "right") return VK_RIGHT;
    if (name.size() == 1) return (WORD)toupper((unsigned char)name[0]);
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::vector<WORD> keys;
        std::string chord = argv[i], part;
        for (size_t k = 0; k <= chord.size(); k++) {
            if (k == chord.size() || chord[k] == '+') {
                WORD vk = VkFor(part);
                if (!vk) {
                    std::printf("unknown key '%s'\n", part.c_str());
                    return 2;
                }
                keys.push_back(vk);
                part.clear();
            } else {
                part += (char)tolower((unsigned char)chord[k]);
            }
        }
        for (WORD vk : keys) {
            Key(vk, false);
            Sleep(25);
        }
        for (size_t k = keys.size(); k-- > 0;) {
            Key(keys[k], true);
            Sleep(25);
        }
        Sleep(300);  // longer than every hotkey dedupe window
    }
    return 0;
}
