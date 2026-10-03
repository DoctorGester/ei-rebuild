#pragma once

// Interpolation inside a deformation cage (the figure manager's morphing, 005ACB80): a point at
// parameters (t, u, w) between the corners of a cube (8 points) or a prism (6 points) read from a
// record stream. Names provisional; see docs/notes/map_objects.md ("Cages").

#include <cstddef>
#include <cstdint>

// The interpolating object (only these fields): the result and the three parameters.
struct CageInterpolator {
    // 0050B8C0. The point at (t, u, w) in the cube of the next 8 points of `*stream` (0x60 bytes,
    // consumed): edges 0-1 and 2-3 at t, between them at u; edges 4-5 and 6-7 likewise; then
    // between the two at w.
    void InterpolateCube(const float** stream);
    // 0050BC00. The same in the prism of the next 6 points (0x48 bytes): edge 0-1 at t, towards
    // point 2 at u; edge 3-4 at t, towards point 5 at u; then between the two at w.
    void InterpolatePrism(const float** stream);

    std::uint8_t unknown_00[0x5C];
    float point[3];     // +0x5C the result
    float t;            // +0x68
    float u;            // +0x6C
    float w;            // +0x70
};
static_assert(offsetof(CageInterpolator, point) == 0x5C);
static_assert(offsetof(CageInterpolator, w) == 0x70);
