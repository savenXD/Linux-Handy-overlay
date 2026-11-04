#pragma once

#include "common.h"
#include <string>

namespace cronq {
namespace geometry {

struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};
void FitPhone(const Rect& workArea, int margin, int* width, int* height);

// Pure function computing the overlay's (x, y) coordinates based on the monitor's work area,
// overlay dimensions (w, h), margin, and corner name ("bottom-left", "bottom-right", "top-left",
// "top-right"). Defaults to bottom-left if corner is unrecognized or empty.
void CalcCorner(const Rect& workArea, int w, int h, int margin, const std::string& corner, int* outX,
                int* outY);

// Fallback monitor index selection: returns the supplied primary index (default 0) if configured index is out
// of bounds or negative.
int SafeMonitorIndex(int requestedIndex, int monitorCount, int primaryIndex = 0);

}  // namespace geometry
}  // namespace cronq
