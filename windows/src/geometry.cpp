#include "geometry.h"
#include <algorithm>

namespace cronq {
namespace geometry {
void FitPhone(const Rect& workArea, int margin, int* width, int* height) {
    const int availableW = std::max(1, workArea.right - workArea.left - 2 * margin);
    const int availableH = std::max(1, workArea.bottom - workArea.top - 2 * margin);
    // Leave room around the companion, including on scaled laptop displays.
    const int maxH = std::max(1, availableH * 3 / 4);
    const double scale = std::min({1.0, double(availableW) / std::max(1, *width),
                                 double(maxH) / std::max(1, *height)});
    *width = std::max(1, int(*width * scale));
    *height = std::max(1, int(*height * scale));
}

void CalcCorner(const Rect& workArea, int w, int h, int margin, const std::string& corner, int* outX,
                int* outY) {
    int x = workArea.left + margin;
    int y = workArea.bottom - h - margin;  // Default: bottom-left

    if (EqNoCase(corner, "bottom-right")) {
        x = workArea.right - w - margin;
        y = workArea.bottom - h - margin;
    } else if (EqNoCase(corner, "top-left")) {
        x = workArea.left + margin;
        y = workArea.top + margin;
    } else if (EqNoCase(corner, "top-right")) {
        x = workArea.right - w - margin;
        y = workArea.top + margin;
    }

    x = std::clamp(x, workArea.left, std::max(workArea.left, workArea.right - w));
    y = std::clamp(y, workArea.top, std::max(workArea.top, workArea.bottom - h));
    if (outX) *outX = x;
    if (outY) *outY = y;
}

int SafeMonitorIndex(int requestedIndex, int monitorCount, int primaryIndex) {
    if (requestedIndex < 0 || requestedIndex >= monitorCount) {
        return primaryIndex >= 0 && primaryIndex < monitorCount ? primaryIndex : 0;
    }
    return requestedIndex;
}

}  // namespace geometry
}  // namespace cronq
