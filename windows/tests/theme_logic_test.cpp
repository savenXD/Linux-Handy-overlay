#include "theme_logic.h"
#include <cstdio>
using namespace cronq::theme_logic;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main() {
    CHECK(MakePalette(1).light);
    CHECK(!MakePalette(0).light);
    CHECK(Hex(MakePalette(1).background)=="#ffffff");
    CHECK(Hex(MakePalette(0).background)=="#000000");
    // Test the full sampled RGB grid: text must remain legible on every surface.
    for (int r=0;r<256;r+=17) for(int g=0;g<256;g+=17) for(int b=0;b<256;b+=17) {
        auto p=MakePalette(2,{r,g,b});
        CHECK(Contrast(p.background,p.text)>=4.5);
        CHECK(Contrast(p.surface,p.text)>=4.5);
        CHECK(Contrast(p.raised,p.text)>=4.5);
        CHECK(Contrast(p.background,p.muted)>=3.0);
    }
    CHECK(Hex(SurroundingColor({{10,20,30},{11,21,31},{255,255,255}}))=="#0a141e");
    CHECK(Hex(SurroundingColor({}))=="#282c34");
    std::puts("theme_logic_test: all checks passed");
}
