#pragma once

// Screen-space triangle clipping for the mesh renderer's software clip path: TL vertices; the clipped polygon is
// re-triangulated as a fan into the caller's index list, new vertices appended to the caller's vertex array. See
// docs/notes/core.md.

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
// The transform's projections write a byte of six outside bits per vertex (ClipTriangle); which plane each is
// there is not read.
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

namespace clip {

// The work buffer: a word array (g_workWords, clip.cpp), the number of indices in it and the last
// intersection parameter.
std::uint16_t* Buffer();
inline int g_bufferCount = /*0079B68C*/ {};
inline float g_lastRatio = /*0079B664*/ {};

// `out` = a + (b - a) * t for position, rhw and texture coordinates; colour and specular per byte, each step
// rounded through a float, then to the nearest integer.
void LerpVertex(const TLVertex* a, const TLVertex* b, float t, TLVertex* out);

// The six plane clippers. Each clips the polygon `indices[0..*count)` against one plane, writing the result back
// to `indices` and `*count`; intersection vertices are appended at `vertices[*vertexCount]`. Returns the new
// count. The first argument (the triangle's combined outcode) is not used. KeepBelow keeps coordinates <= limit,
// KeepAbove > limit.
int ClipXBelow(int, TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit);
int ClipXAbove(int, TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit);
int ClipYBelow(int, TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit);
int ClipYAbove(int, TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit);
int ClipZBelow(int, TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit);
int ClipZAbove(int, TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit);

// Clips the triangle `tri[0..2]` to 0 <= x <= xMax, 0 <= y <= yMax, 0 <= z <= 1.
// `outcodes[i]` holds vertex i's six outside bits (ClipOutcode::All). Returns -1 (all three outside one plane:
// *indexCount = 0), 0 (all inside: *indexCount = 3) or 1 (clipped: `tri` holds *indexCount
// indices, a fan of triangles).
int ClipTriangle(TLVertex* vertices, const std::uint8_t* outcodes, std::uint16_t* tri,
                            int* vertexCount, int* indexCount, float xMax, float yMax);

} // namespace clip
