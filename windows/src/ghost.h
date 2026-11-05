#pragma once

#include "common.h"
#include "ghost_logic.h"

namespace cronq {
namespace ghost {

bool Init();
void Cleanup();

void Run();
void Cancel();
bool IsTyping();
bool IsGuardActive();
void OnForegroundChanged(HWND hwnd);
void OnProgressTick();
void OnFocusPoll();

ghost_logic::Timing GetTiming();
void SetTiming(ghost_logic::Timing t);

// UI integration
void OnGhostStartRequested();
void OnGhostText(const std::string& text);
void OnGhostSettings(bool human, int speed, bool safe);

// Timers
constexpr UINT_PTR kGhostCountdownTimerId = 1004;
constexpr UINT_PTR kGhostSettleTimerId = 1005;
constexpr UINT_PTR kGhostStepTimerId = 1006;
constexpr UINT_PTR kGhostGuardDisengageTimerId = 1007;
constexpr UINT_PTR kGhostProgressTimerId = 1008;
// While typing: checks the foreground window every ~16 ms, so a brief switch to another window is
// caught even if the system's foreground notification arrives late (or not at all).
constexpr UINT_PTR kGhostFocusPollTimerId = 1009;

void OnCountdownTick();
void OnSettleTick();
void StepTyping();
void OnGuardDisengageTimeout();

}  // namespace ghost
}  // namespace cronq
