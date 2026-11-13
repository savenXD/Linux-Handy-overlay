#pragma once

#include "common.h"

namespace cronq {
namespace snap {

constexpr UINT_PTR kSnapCaptureTimerId = 1002;
constexpr UINT_PTR kSnapClipboardTimerId = 1010;
constexpr UINT_PTR kSnapWorkerTimerId = 1011;
constexpr UINT_PTR kSnapRevertTimerId = 1003;

bool Init();
void Cleanup();
void Run();
void RunExperimental();  // Alt+O: WGC-only, temporary Windows 11 test
void RunExperimentalWindow();  // Alt+Shift+O: foreground-window WGC
bool IsSnapping();

void OnSnapTimeout();
void OnSnapRevert();
void OnClipboardRetry();
void OnWorkerPoll();
void OnEncoded(void* job);  // WM_CRONQ_SNAP_DONE (lParam) from the capture worker

}  // namespace snap
}  // namespace cronq
