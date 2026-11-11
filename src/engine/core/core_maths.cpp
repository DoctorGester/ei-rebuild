#include "engine/core/core_maths.h"

#include "lib/crt.h"
#include "lib/float_select.h"

#include <cmath>
#include <cstdint>
#include <cstring>

float ClampUnit(float v) {
    if (v > 1.0f) return 1.0f;
    return MaxKeepB(v, -1.0f);   // -1 also for NaN
}

void CubicSegment::SetBSpline(float p0, float p1, float p2, float p3) {
    c[0] = ((p1 * 4.0f + p0) + p2) * 0.16666667f;
    c[1] = (p2 - p0) * 0.5f;
    c[2] = ((p0 - p1 * 2.0f) + p2) * 0.5f;
    c[3] = (((p1 - p2) * 3.0f - p0) + p3) * 0.16666667f;
}

float CubicSegment::Evaluate(float t) const {
    return ((t * c[3] + c[2]) * t + c[1]) * t + c[0];
}
