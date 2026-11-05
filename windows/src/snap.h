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
void OnEncoded(void* job);  // WM_CRONQ_SNAP_DONE (lParam) from the encoder thread

}  // namespace snap
}  // namespace cronq
