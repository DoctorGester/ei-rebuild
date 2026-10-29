// Screen-space triangle clipping. See clip.h.
#include "clip.h"

#include "core_maths.h"

#include "ei/x87.h"

#include <cstdlib>
#include <cstring>
#include <vector>
#include "lib/static_object.h"

namespace {


// The intersection parameter t = (limit - a) / (b - a): the value is stored to `store` as a float while
// the unrounded quotient is compared with 1. Returns non-zero when t > 1 (above, not equal or unordered).
int StoreRatio(float limit, float a, float b, float* store) {
    const double t = (static_cast<double>(limit) - a) / (static_cast<double>(b) - a);
    *store = static_cast<float>(t);
    return t > 1.0f ? 1 : 0;
}

// One colour byte: a + fistp(float((b - a) * t)).
std::uint32_t LerpByte(std::uint32_t a, std::uint32_t b, float t) {
    int diff = static_cast<int>(b) - static_cast<int>(a);
    return static_cast<std::uint32_t>(x87::fistp(static_cast<float>(static_cast<float>(diff) * t))) + a;
}

// The bytes are computed in the order 2, 1, 0, 3 and ored unmasked.
std::uint32_t LerpColor(std::uint32_t a, std::uint32_t b, float t) {
    std::uint32_t c2 = LerpByte((a >> 16) & 0xFF, (b >> 16) & 0xFF, t);
    std::uint32_t c1 = LerpByte((a >> 8) & 0xFF, (b >> 8) & 0xFF, t);
    std::uint32_t c0 = LerpByte(a & 0xFF, b & 0xFF, t);
    std::uint32_t c3 = LerpByte(a >> 24, b >> 24, t);
    return (c3 << 24) | (c1 << 8) | (c2 << 16) | c0;
}

// The work buffer: a word array.
std::vector<std::uint16_t> g_workWords;
void Emit(std::uint16_t index) { clip::Buffer()[clip::g_bufferCount++] = index; }

template <std::size_t Axis>
float Coord(const TLVertex& v) {
    return (&v.x)[Axis];
}

// The shared body of the six clippers. A vertex is kept when it is inside (KeepAbove: coordinate
// > limit; otherwise coordinate <= limit, NaN counting as <=). An edge crossing the plane adds the
// intersection, or the edge's end vertex when t is above 1, below 0 or NaN.
template <std::size_t Axis, bool KeepAbove>
int ClipPlane(TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    clip::g_bufferCount = 0;
    for (int i = 0; i < *count; ++i) {
        std::uint16_t cur = indices[i];
        std::uint16_t next = indices[(i + 1) % *count];
        const TLVertex& a = vertices[cur];
        const TLVertex& b = vertices[next];
        bool curOver = Coord<Axis>(a) > limit;
        if (KeepAbove ? curOver : !curOver) Emit(cur);
        if (curOver == (Coord<Axis>(b) > limit)) continue;
        bool aboveOne = StoreRatio(limit, Coord<Axis>(a), Coord<Axis>(b), &clip::g_lastRatio) != 0;
        if (!aboveOne && clip::g_lastRatio >= 0.0f) {
            clip::LerpVertex(&a, &b, clip::g_lastRatio, &vertices[*vertexCount]);
            Emit(static_cast<std::uint16_t>(*vertexCount));
            ++*vertexCount;
        } else {   // also a NaN ratio
            Emit(next);
        }
    }
    std::memcpy(indices, clip::Buffer(), static_cast<std::size_t>(clip::g_bufferCount) * 2);
    *count = clip::g_bufferCount;
    return clip::g_bufferCount;
}

} // namespace

namespace clip {

std::uint16_t* Buffer() { return g_workWords.data(); }

void LerpVertex(const TLVertex* a, const TLVertex* b, float t, TLVertex* out) {
    out->x = (b->x - a->x) * t + a->x;
    out->y = (b->y - a->y) * t + a->y;
    out->z = (b->z - a->z) * t + a->z;
    out->rhw = (b->rhw - a->rhw) * t + a->rhw;
    out->u = (b->u - a->u) * t + a->u;
    out->v = (b->v - a->v) * t + a->v;
    out->color = LerpColor(a->color, b->color, t);
    out->specular = LerpColor(a->specular, b->specular, t);
}

int ClipXBelow(int, TLVertex* v, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    return ClipPlane<0, false>(v, vertexCount, indices, count, limit);
}
int ClipXAbove(int, TLVertex* v, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    return ClipPlane<0, true>(v, vertexCount, indices, count, limit);
}
int ClipYBelow(int, TLVertex* v, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    return ClipPlane<1, false>(v, vertexCount, indices, count, limit);
}
int ClipYAbove(int, TLVertex* v, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    return ClipPlane<1, true>(v, vertexCount, indices, count, limit);
}
int ClipZBelow(int, TLVertex* v, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    return ClipPlane<2, false>(v, vertexCount, indices, count, limit);
}
int ClipZAbove(int, TLVertex* v, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    return ClipPlane<2, true>(v, vertexCount, indices, count, limit);
}

int ClipTriangle(TLVertex* vertices, const std::uint8_t* outcodes, std::uint16_t* tri,
                            int* vertexCount, int* indexCount, float xMax, float yMax) {
    std::uint8_t c0 = outcodes[tri[0]];
    std::uint8_t c1 = outcodes[tri[1]];
    std::uint8_t c2 = outcodes[tri[2]];
    int combined = c0 | c1 | c2;
    g_workWords.resize(0x20);
    *indexCount = 3;
    if (c0 & c1 & c2 & 0x3F) {
        *indexCount = 0;
        return -1;
    }
    if (!combined) {
        *indexCount = 3;
        return 0;
    }
    ClipXBelow(combined, vertices, vertexCount, tri, indexCount, xMax);
    ClipXAbove(combined, vertices, vertexCount, tri, indexCount, 0.0f);
    ClipYAbove(combined, vertices, vertexCount, tri, indexCount, 0.0f);
    ClipYBelow(combined, vertices, vertexCount, tri, indexCount, yMax);
    ClipZBelow(combined, vertices, vertexCount, tri, indexCount, 1.0f);
    ClipZAbove(combined, vertices, vertexCount, tri, indexCount, 0.0f);
    // Fan: (0, i - 1, i) for every further vertex of the clipped polygon.
    *indexCount = 0;
    for (int i = 2; i < g_bufferCount; ++i) {
        tri[(*indexCount)++] = Buffer()[0];
        tri[(*indexCount)++] = Buffer()[i - 1];
        tri[(*indexCount)++] = Buffer()[i];
    }
    return 1;
}

} // namespace clip
