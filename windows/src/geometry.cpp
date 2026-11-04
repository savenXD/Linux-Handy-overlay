#include "geometry.h"

namespace cronq {
namespace geometry {

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
