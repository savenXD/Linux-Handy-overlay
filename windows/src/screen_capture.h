#pragma once
#include <atomic>
#include <vector>

#include "common.h"

namespace cronq {
namespace screen_capture {
enum class Status { Ok, Unavailable, Blank, ProtectedContent, DesktopUnavailable, Cancelled, CaptureDenied };
enum class Backend {
    Automatic,
    Dxgi,
    Gdi,
    Wgc,
    WgcWindow
};  // Wgc is temporary and explicit; never an automatic fallback
struct Result {
    Status status = Status::Unavailable;
    int width = 0, height = 0;
    std::vector<uint8_t> bgra;
};
// One shot, bounded waits, no cached textures or duplication sessions across
// snapshots. Captures every intersecting physical output on its owning adapter,
// then composes bounds.
Result Capture(const RECT& bounds, HWND foreground, const std::atomic_bool& cancelled,
               Backend backend = Backend::Automatic);
const char* Message(Status status);
Result CaptureWindow(HWND window, const std::atomic_bool& cancelled);
}  // namespace screen_capture
}  // namespace cronq
