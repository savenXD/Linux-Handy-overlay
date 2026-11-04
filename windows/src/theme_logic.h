#pragma once
#include <string>
#include <vector>
namespace cronq::theme_logic {
struct Color { int r, g, b; };
struct Palette {
    Color background, surface, raised, text, muted, border;
    bool light;
};
double Luminance(Color c);
double Contrast(Color a, Color b);
Color SurroundingColor(const std::vector<Color>& samples);
Palette MakePalette(int mode, Color surrounding = {40, 44, 52});
std::string Hex(Color c);
}
