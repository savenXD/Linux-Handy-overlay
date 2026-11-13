#pragma once
#include "screen_capture.h"

namespace cronq {
namespace wgc_capture {
// Temporary Windows 11 experiment. Uses only WGC, with the system capture border enabled.
screen_capture::Status CaptureMonitor(HMONITOR monitor, const RECT& bounds, const std::atomic_bool& cancelled,
                                      ULONGLONG deadline, std::vector<uint8_t>* pixels);
screen_capture::Result CaptureWindow(HWND window, const std::atomic_bool& cancelled, ULONGLONG deadline);
}  // namespace wgc_capture
}  // namespace cronq
