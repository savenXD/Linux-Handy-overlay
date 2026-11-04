#pragma once

#include "common.h"

namespace cronq {
namespace snap {

constexpr UINT_PTR kSnapCaptureTimerId = 1002;
constexpr UINT_PTR kSnapRevertTimerId = 1003;

bool Init();
void Cleanup();
void Run();
bool IsSnapping();

void OnSnapTimeout();
void OnSnapRevert();

}  // namespace snap
}  // namespace cronq
