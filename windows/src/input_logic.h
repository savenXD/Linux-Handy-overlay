#pragma once
#include <array>
namespace cronq {
namespace input_logic {
struct Context {
    bool target = false, exclusive = false, shield = false, ctrlOnly = false, guard = false, hotkey = false;
    // Windows' own key state before this event: false means the key is really up, so a
    // remembered "held" entry is stale (its release went to a secure desktop or an elevated
    // window, where the hook never runs) and this press must be treated as a fresh one.
    bool osHeld = true;
};
struct Route {
    bool swallow = false, forward = false, clipboard = false, hotkey = false, repeat = false;
};
class Router {
   public:
    Route Key(unsigned vk, bool down, const Context&);
    void Reset() { held_.fill(Held::None); }

   private:
    enum class Held { None, Pass, Mirror, Exclusive, Shield, Hotkey };
    std::array<Held, 256> held_{};
};

// Modifier / chord keys whose "down" flag the hook tracks itself.
constexpr unsigned kTrackedKeys[] = {0xA4, 0xA5, 0x12,   // LMENU RMENU MENU
                                     0xA2, 0xA3, 0x11,   // LCONTROL RCONTROL CONTROL
                                     0xA0, 0xA1, 0x10,   // LSHIFT RSHIFT SHIFT
                                     0x5B, 0x5C,         // LWIN RWIN
                                     'K', 'X'};

// Clears tracked keys the hook believes are down but Windows reports as up. Keys whose press
// the hook swallowed are skipped: Windows never saw them go down, so it cannot confirm them.
// osDown(vk) must return Windows' asynchronous key state (true = down).
template <typename OsDown>
void ClearStaleKeys(bool (&physicalDown)[256], const bool (&swallowed)[256], unsigned currentVk, OsDown osDown) {
    for (unsigned vk : kTrackedKeys) {
        if (vk == currentVk || !physicalDown[vk] || swallowed[vk]) continue;
        if (!osDown(vk)) physicalDown[vk] = false;
    }
}
}  // namespace input_logic
}  // namespace cronq
