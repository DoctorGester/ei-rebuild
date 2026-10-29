#pragma once

// The pre-transformed (TL) vertex and the clip-space outcodes of the transform's projections. Nothing clips on the
// CPU: the GPU clips what crosses the view (docs/notes/renderer.md, section 10).

#include "lib/bit_flags.h"
#include "lib/float_select.h"   // SignBit

#include <cstddef>
#include <cstdint>

struct TLVertex {
    float x, y, z, rhw;
    std::uint32_t color;      // ARGB
    std::uint32_t specular;   // ARGB
    float u, v;
};
static_assert(sizeof(TLVertex) == 0x20);
static_assert(offsetof(TLVertex, color) == 0x10);

// A point's clip outcode: a bit per clip plane it is outside of (CTransformPipeline::ClipTest's corners, model.cpp).
// The transform's projections write a byte of six outside bits per vertex; which plane each is there is not read.
enum class ClipOutcode : std::uint32_t {
    XLow = 0x1,     // X + W < 0
    XHigh = 0x2,    // 2W < X + W (or unordered)
    YLow = 0x4,     // Y + W < 0
    YHigh = 0x8,    // 2W < Y + W (or unordered)
    ZNear = 0x10,   // Z < 0
    ZFar = 0x20,    // W < Z (or unordered)
    All = XLow | XHigh | YLow | YHigh | ZNear | ZFar,
};
template <> inline constexpr bool lib::kBitFlags<ClipOutcode> = true;

// The outcode of the clip-space point (x, y, z, w): the "< 0" tests are sign tests (-0 is outside), the others
// count an unordered compare as outside.
inline std::uint32_t ClipCodeOf(float x, float y, float z, float w) {
    const float twoW = w + w;
    const float xw = x + w;
    const float yw = y + w;
    std::uint32_t code = 0;
    if (SignBit(xw)) code |= static_cast<std::uint32_t>(ClipOutcode::XLow);
    if (!(twoW >= xw)) code |= static_cast<std::uint32_t>(ClipOutcode::XHigh);
    if (SignBit(yw)) code |= static_cast<std::uint32_t>(ClipOutcode::YLow);
    if (!(twoW >= yw)) code |= static_cast<std::uint32_t>(ClipOutcode::YHigh);
    if (SignBit(z)) code |= static_cast<std::uint32_t>(ClipOutcode::ZNear);
    if (!(w >= z)) code |= static_cast<std::uint32_t>(ClipOutcode::ZFar);
    return code;
}

// The verdict on a batch of points from their outcodes: 2 when every point is outside one plane (also for no
// points), 0 when none is outside any, else 1 (the batch crosses the view's edges).
struct ClipVerdict {
    std::uint32_t all = static_cast<std::uint32_t>(ClipOutcode::All);
    std::uint32_t any = 0;

    void Add(std::uint32_t code) {
        all &= code;
        any |= code;
    }
    int Result() const { return all != 0 ? 2 : any != 0 ? 1 : 0; }
};
