// 004E59F0: the zone exit's area test (world.h, ZoneExit). The rectangle is turned by `angle` about
// its centre: the point is turned the other way first. Comparisons keep the original's NaN cases:
// a NaN angle takes the axis-aligned path, a NaN turned x is outside, the other tests let NaN in.
#include "world.h"

#include "core_maths.h"

#include "ei/hook.h"

#include <cmath>

bool ZoneExit::Contains(float x, float y) const {
    if (!(angle < 0.0f) && !(angle > 0.0f)) {
        if (x1 > x) return false;
        if (x > x2) return false;
        if (y1 > y) return false;
        return !(y > y2);
    }
    double centerX = (x2 + x1) * 0.5f;
    float centerY = StoreFloat(static_cast<float>((y2 + y1) * 0.5f));
    float c = StoreFloat(static_cast<float>(std::cos(static_cast<double>(angle))));
    float s = StoreFloat(static_cast<float>(std::sin(static_cast<double>(angle))));
    float dy = StoreFloat(y - centerY);
    double dxExact = x - centerX;
    float dx = StoreFloat(static_cast<float>(dxExact));
    double turnedX = dxExact * c + dy * s + centerX;
    float turnedY = StoreFloat(static_cast<float>(centerY - dx * s + dy * c));
    if (!(turnedX >= x1)) return false;
    if (turnedX > x2) return false;
    if (y1 > turnedY) return false;
    return !(turnedY > y2);
}
EI_REPLACE_METHOD(0x004E59F0, ZoneExit::Contains);
