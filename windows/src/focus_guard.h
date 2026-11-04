#pragma once
#include "common.h"
namespace cronq {
namespace focus_guard {
bool Init(HWND overlay);
void Cleanup();
}  // namespace focus_guard
}  // namespace cronq
