#pragma once

#include "maths.h"

#include <cstdint>

// Six corner positions of a bone, from a .bon file. A figure's proportions are set by three morph
// coefficients; the bone position is interpolated between these corners:
//   lower = lerp(lerp(c[0], c[1], s), c[2], t)
//   upper = lerp(lerp(c[3], c[4], s), c[5], t)
//   bone  = lerp(lower, upper, u)
struct BoneMorphBox {
    Vector3 c[6];
};
static_assert(sizeof(BoneMorphBox) == 0x48);

// An animated node of a figure (one bone). Name and layout provisional; only the morph fields are known.
class CAnimationNode {
public:
    // 005064C0. Stores the morph coefficients and computes the bone position from `box`.
    void SetMorph(const BoneMorphBox* box, float s, float t, float u);

    std::uint8_t unknown_000[0x110];
    Vector3 position_;      // +0x110
    float morphS_;          // +0x11C
    float morphT_;          // +0x120
    float morphU_;          // +0x124
};
static_assert(sizeof(CAnimationNode) == 0x128);
