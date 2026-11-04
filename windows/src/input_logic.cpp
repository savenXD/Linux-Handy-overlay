#include "input_logic.h"
namespace cronq {
namespace input_logic {
Route Router::Key(unsigned vk, bool down, const Context& c) {
    if (vk >= held_.size()) return {};
    Held kind = held_[vk];
    const bool repeat = down && kind != Held::None;
    if (down && !repeat) {
        if (c.hotkey)
            kind = Held::Hotkey;
        else if (c.guard || !c.target)
            kind = Held::Pass;
        else if (c.shield && c.ctrlOnly && (vk == 'A' || vk == 'C' || vk == 'V' || vk == 'X'))
            kind = Held::Shield;
        else
            kind = c.exclusive ? Held::Exclusive : Held::Mirror;
        held_[vk] = kind;
    }
    if (!down) held_[vk] = Held::None;
    Route r;
    r.swallow = kind == Held::Exclusive || kind == Held::Shield || kind == Held::Hotkey;
    r.forward = kind == Held::Exclusive || kind == Held::Mirror;
    r.clipboard = kind == Held::Shield;
    r.hotkey = kind == Held::Hotkey && down;
    r.repeat = repeat;
    return r;
}
}  // namespace input_logic
}  // namespace cronq
