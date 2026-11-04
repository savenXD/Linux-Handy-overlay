// The overlay window: always on top, never activated, rounded, translucent, click-through on demand.
#pragma once

#include "common.h"

namespace cronq {
namespace overlay {

constexpr int kWidth = 360;
constexpr int kHeight = 640;
constexpr int kMinWidth = 260;
constexpr int kMinHeight = 180;
constexpr int kCompactHeight = 280;
constexpr int kCornerRadius = 24;

bool Init(bool startVisible);
void Cleanup();
HWND Hwnd();

void Show();
void Hide();
void Toggle();
bool IsVisible();

void Reposition();
void DockBottom(bool right); // instant bottom corner on the configured display, saved for the next launch
void ApplyConfig();
void SetOpacity(double opacity);  // clamps to 0.20 .. 1.00 and shows the "Opacity: N%" toast
double GetOpacity();
void SetSize(int width, int height);
void SetFullscreen();             // toggles
void SetDefaultSize();
void GetGeometry(int* x, int* y, int* w, int* h);

void SetGhost(bool ghost);        // WS_EX_TRANSPARENT on/off
void QueueDraw();
void RefreshAppearance();
void ShowToast(const std::string& msg);

// Capture protection (WDA_EXCLUDEFROMCAPTURE). The overlay stays visible to you but not to recorders / screen sharing.
bool SetCaptureHidden(bool hidden);   // main overlay window; false if Windows refuses (needs 10 2004+)
void ProtectAllWindows();             // also marks popups/menus of Cronq and its WebView2 processes
unsigned CaptureAffinity();   // affinity value to restore after a temporary change (snap)

double Scale();
void BeginExternalDrag(bool move, int edges);
void ToggleHeight();
void Back();
void UniversalReset();
void SwitchApp(const std::string& name);
const char* CurrentApp();

}  // namespace overlay
}  // namespace cronq
