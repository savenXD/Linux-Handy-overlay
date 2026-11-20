#pragma once

#include "common.h"
#include "config.h"

namespace cronq {
namespace input {

enum class TypingTarget { None = 0, Overlay = 1 };
enum class GhostMode { Off = 0, On = 1 };
enum class GrabStatus { None = 0, Active = 1, Failed = 2 };

bool Init(HWND target);
void Cleanup();

void ToggleTypingTarget();

TypingTarget GetTypingTarget();
void SetTypingTarget(TypingTarget target);

GrabStatus GetGrabStatus();
void ReleaseGrab();

GhostMode GetGhostMode();
void SetGhostMode(GhostMode ghost);
void ToggleGhostMode();

void OnMouseClick(bool inside);

}  // namespace input
}  // namespace cronq
