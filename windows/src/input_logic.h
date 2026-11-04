#pragma once
#include <array>
namespace cronq {
namespace input_logic {
struct Context {
    bool target = false, exclusive = false, shield = false, ctrlOnly = false, guard = false, hotkey = false;
};
struct Route {
    bool swallow = false, forward = false, clipboard = false, hotkey = false, repeat = false;
};
class Router {
   public:
    Route Key(unsigned vk, bool down, const Context&);

   private:
    enum class Held { None, Pass, Mirror, Exclusive, Shield, Hotkey };
    std::array<Held, 256> held_{};
};
}  // namespace input_logic
}  // namespace cronq
