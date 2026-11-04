#pragma once
#include <string>
namespace cronq {
namespace ui {
void PostToUi(const std::string&);
void PushState();
void OnMessage(const std::string&);
bool IsReady();
void Reset();
}  // namespace ui
}  // namespace cronq
