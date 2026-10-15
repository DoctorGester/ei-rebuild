// Interpolation inside a deformation cage (the figure manager's morphing, 005ACB80): a point at
// parameters (t, u, w) between the corners of a cube (8 points) or a prism (6 points) read from a
// record stream. Names provisional; see docs/notes/map_objects.md ("Cages").
#include "map_object_cage.h"

#include "ei/hook.h"
#include "lib/vecmath.h"

// Every step is rounded to float where the original stores (its Vector3 constructor arguments,
// the vector helpers' results); values it keeps on the FPU stack are doubles.

namespace {
float Sub(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }
float Add(float a, float b) { return static_cast<float>(static_cast<double>(a) + b); }
float Mul(float a, float b) { return static_cast<float>(static_cast<double>(a) * b); }

// from + (to - from) * s, every step stored: the original's Vector3 chain.
Vector3 Lerp(const float* from, const float* to, float s) {
    Vector3 d{Sub(to[0], from[0]), Sub(to[1], from[1]), Sub(to[2], from[2])};
    Vector3 scaled{Mul(d.x, s), Mul(d.y, s), Mul(d.z, s)};
    return Vector3{Add(scaled.x, from[0]), Add(scaled.y, from[1]), Add(scaled.z, from[2])};
}
} // namespace

void CageInterpolator::InterpolateCube(const float** stream) {
    const float* p = *stream;
    Vector3 a = Lerp(p + 0, p + 3, t);
    point.x = a.x;
    point.y = a.y;
    point.z = a.z;
    Vector3 b = Lerp(p + 6, p + 9, t);
    Vector3 towards = Lerp(point, b, u);   // (b - point) * u + point, vecmath.h
    point.x = towards.x;
    point.y = towards.y;
    point.z = towards.z;

    const float* q = p + 12;
    Vector3 c = Lerp(q + 0, q + 3, t);
    Vector3 e = Lerp(q + 6, q + 9, t);
    // SubtractVectors (e - c), ScaleVector (u * that), AddInPlace (that + c).
    Vector3 ec{Sub(e.x, c.x), Sub(e.y, c.y), Sub(e.z, c.z)};
    Vector3 ecScaled{Mul(u, ec.x), Mul(u, ec.y), Mul(u, ec.z)};
    c = Vector3{Add(ecScaled.x, c.x), Add(ecScaled.y, c.y), Add(ecScaled.z, c.z)};
    Vector3 cp{Sub(c.x, point.x), Sub(c.y, point.y), Sub(c.z, point.z)};
    Vector3 cpScaled{Mul(w, cp.x), Mul(w, cp.y), Mul(w, cp.z)};
    point.x = Add(cpScaled.x, point.x);
    point.y = Add(cpScaled.y, point.y);
    point.z = Add(cpScaled.z, point.z);
    *stream += 0x60 / sizeof(float);
}
EI_REPLACE_METHOD(0x0050B8C0, CageInterpolator::InterpolateCube);

void CageInterpolator::InterpolatePrism(const float** stream) {
    const float* p = *stream;
    // Edge 0-1 at t: x keeps its difference and product unrounded, y and z are stored.
    double dx = static_cast<double>(p[3]) - p[0];
    float dy = Sub(p[4], p[1]);
    float dz = Sub(p[5], p[2]);
    double sx = dx * t;
    float sy = Mul(dy, t);
    float sz = Mul(dz, t);
    point.x = static_cast<float>(sx + p[0]);
    point.y = Add(sy, p[1]);
    point.z = Add(sz, p[2]);
    // Towards point 2 at u: the differences unrounded, the products stored.
    float vz = static_cast<float>((static_cast<double>(p[8]) - point.z) * u);
    float vy = static_cast<float>((static_cast<double>(p[7]) - point.y) * u);
    float vx = static_cast<float>((static_cast<double>(p[6]) - point.x) * u);
    point.x = Add(vx, point.x);
    point.y = Add(vy, point.y);
    point.z = Add(vz, point.z);

    // Edge 3-4 at t, towards point 5 at u.
    Vector3 b = Lerp(p + 9, p + 12, t);
    Vector3 e{Sub(p[15], b.x), Sub(p[16], b.y), Sub(p[17], b.z)};
    Vector3 f{Mul(e.x, u), Mul(e.y, u), Mul(e.z, u)};
    // Then between the two at w: the sums unrounded, the differences stored.
    float gz = static_cast<float>((static_cast<double>(f.z) + b.z) - point.z);
    float gy = static_cast<float>((static_cast<double>(f.y) + b.y) - point.y);
    float gx = static_cast<float>((static_cast<double>(b.x) + f.x) - point.x);
    Vector3 h{Mul(w, gx), Mul(w, gy), Mul(w, gz)};
    point.x = Add(h.x, point.x);
    point.y = Add(h.y, point.y);
    point.z = Add(h.z, point.z);
    *stream += 0x48 / sizeof(float);
}
EI_REPLACE_METHOD(0x0050BC00, CageInterpolator::InterpolatePrism);
