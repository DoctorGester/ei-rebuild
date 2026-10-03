// Interpolation inside a deformation cage (the figure manager's morphing, 005ACB80): a point at
// parameters (t, u, w) between the corners of a cube (8 points) or a prism (6 points) read from a
// record stream. Names provisional; see docs/notes/map_objects.md ("Cages").
#include "map_object_cage.h"

#include "ei/hook.h"

// Every step is rounded to float where the original stores (its Vector3 constructor arguments,
// the vector helpers' results); values it keeps on the FPU stack are doubles.
#pragma float_control(precise, on)

namespace {
struct V3 {
    float x, y, z;
};

float Sub(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }
float Add(float a, float b) { return static_cast<float>(static_cast<double>(a) + b); }
float Mul(float a, float b) { return static_cast<float>(static_cast<double>(a) * b); }

// from + (to - from) * s, every step stored: the original's Vector3 chain.
V3 Lerp(const float* from, const float* to, float s) {
    V3 d{Sub(to[0], from[0]), Sub(to[1], from[1]), Sub(to[2], from[2])};
    V3 scaled{Mul(d.x, s), Mul(d.y, s), Mul(d.z, s)};
    return V3{Add(scaled.x, from[0]), Add(scaled.y, from[1]), Add(scaled.z, from[2])};
}
} // namespace

void CageInterpolator::InterpolateCube(const float** stream) {
    const float* p = *stream;
    V3 a = Lerp(p + 0, p + 3, t);
    point[0] = a.x;
    point[1] = a.y;
    point[2] = a.z;
    V3 b = Lerp(p + 6, p + 9, t);
    V3 towards = Lerp(point, &b.x, u);   // (b - point) * u + point
    point[0] = towards.x;
    point[1] = towards.y;
    point[2] = towards.z;

    const float* q = p + 12;
    V3 c = Lerp(q + 0, q + 3, t);
    V3 e = Lerp(q + 6, q + 9, t);
    // SubtractVectors (e - c), ScaleVector (u * that), AddInPlace (that + c).
    V3 ec{Sub(e.x, c.x), Sub(e.y, c.y), Sub(e.z, c.z)};
    V3 ecScaled{Mul(u, ec.x), Mul(u, ec.y), Mul(u, ec.z)};
    c = V3{Add(ecScaled.x, c.x), Add(ecScaled.y, c.y), Add(ecScaled.z, c.z)};
    V3 cp{Sub(c.x, point[0]), Sub(c.y, point[1]), Sub(c.z, point[2])};
    V3 cpScaled{Mul(w, cp.x), Mul(w, cp.y), Mul(w, cp.z)};
    point[0] = Add(cpScaled.x, point[0]);
    point[1] = Add(cpScaled.y, point[1]);
    point[2] = Add(cpScaled.z, point[2]);
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
    point[0] = static_cast<float>(sx + p[0]);
    point[1] = Add(sy, p[1]);
    point[2] = Add(sz, p[2]);
    // Towards point 2 at u: the differences unrounded, the products stored.
    float vz = static_cast<float>((static_cast<double>(p[8]) - point[2]) * u);
    float vy = static_cast<float>((static_cast<double>(p[7]) - point[1]) * u);
    float vx = static_cast<float>((static_cast<double>(p[6]) - point[0]) * u);
    point[0] = Add(vx, point[0]);
    point[1] = Add(vy, point[1]);
    point[2] = Add(vz, point[2]);

    // Edge 3-4 at t, towards point 5 at u.
    V3 b = Lerp(p + 9, p + 12, t);
    V3 e{Sub(p[15], b.x), Sub(p[16], b.y), Sub(p[17], b.z)};
    V3 f{Mul(e.x, u), Mul(e.y, u), Mul(e.z, u)};
    // Then between the two at w: the sums unrounded, the differences stored.
    float gz = static_cast<float>((static_cast<double>(f.z) + b.z) - point[2]);
    float gy = static_cast<float>((static_cast<double>(f.y) + b.y) - point[1]);
    float gx = static_cast<float>((static_cast<double>(b.x) + f.x) - point[0]);
    V3 h{Mul(w, gx), Mul(w, gy), Mul(w, gz)};
    point[0] = Add(h.x, point[0]);
    point[1] = Add(h.y, point[1]);
    point[2] = Add(h.z, point[2]);
    *stream += 0x48 / sizeof(float);
}
EI_REPLACE_METHOD(0x0050BC00, CageInterpolator::InterpolatePrism);
