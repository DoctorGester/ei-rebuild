// Interpolation inside a deformation cage (the figure manager's morphing): a point at parameters (t, u, w)
// between the corners of a cube (8 points) or a prism (6 points) read from a record stream: a bone's offset at
// its morph coefficients (FigureBone, map_object_figures.h); see docs/notes/map_objects.md.
#include "map_object_figures.h"

#include "lib/vecmath.h"

namespace {
// from + (to - from) * s of two corners in the stream.
Vector3 Lerp(const float* from, const float* to, float s) {
    return Vector3{::Lerp(from[0], to[0], s), ::Lerp(from[1], to[1], s), ::Lerp(from[2], to[2], s)};
}

Vector3 Corner(const float* p) { return Vector3{p[0], p[1], p[2]}; }
} // namespace

void FigureBone::InterpolateCube(const float** stream) {
    Vector3& point = offset;
    const float t = morphS, u = morphT, w = morphU;
    const float* p = *stream;
    point = Lerp(p + 0, p + 3, t);
    point = Lerp(point, Lerp(p + 6, p + 9, t), u);
    const float* q = p + 12;
    Vector3 c = Lerp(q + 0, q + 3, t);
    Vector3 e = Lerp(q + 6, q + 9, t);
    c = Lerp(c, e, u);
    point = Lerp(point, c, w);
    *stream += 0x60 / sizeof(float);
    *stream += 0x60 / sizeof(float);
}

void FigureBone::InterpolatePrism(const float** stream) {
    Vector3& point = offset;
    const float t = morphS, u = morphT, w = morphU;
    const float* p = *stream;
    // Edge 0-1 at t, towards point 2 at u.
    point = Lerp(p + 0, p + 3, t);
    point = Lerp(point, Corner(p + 6), u);
    // Edge 3-4 at t, towards point 5 at u, then between the two at w.
    Vector3 b = Lerp(p + 9, p + 12, t);
    Vector3 f = (Corner(p + 15) - b) * u;
    point = ((f + b) - point) * w + point;
    *stream += 0x48 / sizeof(float);
}
