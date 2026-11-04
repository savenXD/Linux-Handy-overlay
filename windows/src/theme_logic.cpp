#include "theme_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
namespace cronq::theme_logic {
namespace {
double Linear(int c) { double s = c / 255.0; return s <= .04045 ? s / 12.92 : std::pow((s + .055) / 1.055, 2.4); }
Color Mix(Color a, Color b, double amount) {
    return {int(a.r + (b.r - a.r) * amount), int(a.g + (b.g - a.g) * amount), int(a.b + (b.b - a.b) * amount)};
}
}
double Luminance(Color c) { return .2126 * Linear(c.r) + .7152 * Linear(c.g) + .0722 * Linear(c.b); }
double Contrast(Color a, Color b) { double x=Luminance(a), y=Luminance(b); return (std::max(x,y)+.05)/(std::min(x,y)+.05); }
Color SurroundingColor(const std::vector<Color>& samples) {
    if (samples.empty()) return {40,44,52};
    // Select the most common coarse color rather than averaging unrelated windows into gray.
    struct Bucket { int count=0, r=0, g=0, b=0; };
    std::map<int, Bucket> buckets;
    for (auto c : samples) { auto& b=buckets[((c.r>>5)<<6)|((c.g>>5)<<3)|(c.b>>5)]; ++b.count; b.r+=c.r; b.g+=c.g; b.b+=c.b; }
    Bucket best;
    for (auto entry : buckets) if (entry.second.count > best.count) best=entry.second;
    return {best.r/best.count, best.g/best.count, best.b/best.count};
}
Palette MakePalette(int mode, Color surrounding) {
    Color bg = mode == 1 ? Color{255,255,255} : mode == 2 ? surrounding : Color{0,0,0};
    Color black{0,0,0}, white{255,255,255};
    Color text = Contrast(bg,black) >= Contrast(bg,white) ? black : white;
    bool light = text.r == black.r;
    // Move secondary surfaces away from the text to preserve contrast throughout the palette.
    Color away = light ? white : Color{0,0,0};
    Color surface = Mix(bg,away,.10), raised = Mix(bg,away,.18);
    if (mode==0) { surface={18,18,18}; raised={30,30,30}; }
    if (mode==1) { surface={245,246,248}; raised={230,233,238}; }
    return {bg,surface,raised,text,Mix(text,bg,.20),Mix(text,bg,.72),light};
}
std::string Hex(Color c) { char buf[8]; std::snprintf(buf,sizeof(buf),"#%02x%02x%02x",std::clamp(c.r,0,255),std::clamp(c.g,0,255),std::clamp(c.b,0,255)); return buf; }
}
