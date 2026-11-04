#pragma once
#include "theme_logic.h"
namespace cronq::theme {
void Init();
void Cleanup();
void Reload();
bool SetMode(int mode);
bool ToggleCamouflage();
bool ToggleTone();
bool IsCamouflage();
int Mode();
const theme_logic::Palette& Get();
std::string PaletteJson();
std::string WebScript();
}
