// Screen-space triangle clipping. See clip.h.
#include "clip.h"

#include "core_maths.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstring>
#include "originals_library.h"
#include "game_data.h"

namespace {

const float kOne = 1.0f;   // 0073B84C

// The intersection parameter t = (limit - a) / (b - a), exactly as the original computes it: the
// value is stored to `store` as a float (FST) while the unrounded quotient is compared with 1.
// Returns non-zero when t > 1 (FCOMP; FNSTSW; TEST AH,0x41 clear: above, not equal or unordered).
int StoreRatio(float limit, float a, float b, float* store) {
    const double t = (static_cast<double>(limit) - a) / (static_cast<double>(b) - a);
    *store = static_cast<float>(t);
    return t > kOne ? 1 : 0;
}

// One colour byte: a + fistp(float((b - a) * t)).
std::uint32_t LerpByte(std::uint32_t a, std::uint32_t b, float t) {
    int diff = static_cast<int>(b) - static_cast<int>(a);
    return static_cast<std::uint32_t>(x87::fistp(static_cast<float>(static_cast<float>(diff) * t))) + a;
}

// The original computes the bytes in the order 2, 1, 0, 3 and ors them unmasked.
std::uint32_t LerpColor(std::uint32_t a, std::uint32_t b, float t) {
    std::uint32_t c2 = LerpByte((a >> 16) & 0xFF, (b >> 16) & 0xFF, t);
    std::uint32_t c1 = LerpByte((a >> 8) & 0xFF, (b >> 8) & 0xFF, t);
    std::uint32_t c0 = LerpByte(a & 0xFF, b & 0xFF, t);
    std::uint32_t c3 = LerpByte(a >> 24, b >> 24, t);
    return (c3 << 24) | (c1 << 8) | (c2 << 16) | c0;
}

// The work buffer 0079B668: a CWordArray (SetSize is MFC's, 006FEEFA).
mfc::CArray<std::uint16_t>& WorkWords() { return ei::GameData<mfc::CArray<std::uint16_t>>(0x0079B668); }

void Emit(std::uint16_t index) { clip::Buffer()[clip::BufferCount()++] = index; }

template <std::size_t Axis>
float Coord(const TLVertex& v) {
    return (&v.x)[Axis];
}

// The shared body of the six clippers. A vertex is kept when it is inside (KeepAbove: coordinate
// > limit; otherwise coordinate <= limit, NaN counting as <=). An edge crossing the plane adds the
// intersection, or the edge's end vertex when t is above 1, below 0 or NaN.
template <std::size_t Axis, bool KeepAbove>
int ClipPlane(TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    clip::BufferCount() = 0;
    for (int i = 0; i < *count; ++i) {
        std::uint16_t cur = indices[i];
        std::uint16_t next = indices[(i + 1) % *count];
        const TLVertex& a = vertices[cur];
        const TLVertex& b = vertices[next];
        bool curOver = Coord<Axis>(a) > limit;
        if (KeepAbove ? curOver : !curOver) Emit(cur);
        if (curOver == (Coord<Axis>(b) > limit)) continue;
        bool aboveOne = StoreRatio(limit, Coord<Axis>(a), Coord<Axis>(b), &clip::LastRatio()) != 0;
        if (!aboveOne && clip::LastRatio() >= 0.0f) {
            clip::LerpVertex(&a, &b, clip::LastRatio(), &vertices[*vertexCount]);
            Emit(static_cast<std::uint16_t>(*vertexCount));
            ++*vertexCount;
        } else {   // also a NaN ratio
            Emit(next);
        }
    }
    std::memcpy(indices, clip::Buffer(), static_cast<std::size_t>(clip::BufferCount()) * 2);
    *count = clip::BufferCount();
    return clip::BufferCount();
}

} // namespace

namespace clip {

void __fastcall LerpVertex(const TLVertex* a, const TLVertex* b, float t, TLVertex* out) {
    out->x = (b->x - a->x) * t + a->x;
    out->y = (b->y - a->y) * t + a->y;
    out->z = (b->z - a->z) * t + a->z;
    out->rhw = (b->rhw - a->rhw) * t + a->rhw;
    out->u = (b->u - a->u) * t + a->u;
    out->v = (b->v - a->v) * t + a->v;
    out->color = LerpColor(a->color, b->color, t);
    out->specular = LerpColor(a->specular, b->specular, t);
}
EI_REPLACE(0x0042E4D0, LerpVertex);

int __fastcall ClipXBelow(int, TLVertex* v, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    return ClipPlane<0, false>(v, vertexCount, indices, count, limit);
}
int __fastcall ClipXAbove(int, TLVertex* v, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    return ClipPlane<0, true>(v, vertexCount, indices, count, limit);
}
int __fastcall ClipYBelow(int, TLVertex* v, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    return ClipPlane<1, false>(v, vertexCount, indices, count, limit);
}
int __fastcall ClipYAbove(int, TLVertex* v, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    return ClipPlane<1, true>(v, vertexCount, indices, count, limit);
}
int __fastcall ClipZBelow(int, TLVertex* v, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    return ClipPlane<2, false>(v, vertexCount, indices, count, limit);
}
int __fastcall ClipZAbove(int, TLVertex* v, int* vertexCount, std::uint16_t* indices, int* count, float limit) {
    return ClipPlane<2, true>(v, vertexCount, indices, count, limit);
}
EI_REPLACE(0x0042DB70, ClipXBelow);
EI_REPLACE(0x0042DD00, ClipXAbove);
EI_REPLACE(0x0042DE90, ClipYBelow);
EI_REPLACE(0x0042E020, ClipYAbove);
EI_REPLACE(0x0042E1B0, ClipZBelow);
EI_REPLACE(0x0042E340, ClipZAbove);

int __fastcall ClipTriangle(TLVertex* vertices, const std::uint8_t* outcodes, std::uint16_t* tri,
                            int* vertexCount, int* indexCount, float xMax, float yMax) {
    std::uint8_t c0 = outcodes[tri[0]];
    std::uint8_t c1 = outcodes[tri[1]];
    std::uint8_t c2 = outcodes[tri[2]];
    int combined = c0 | c1 | c2;
    WorkWords().SetSize(0x20, -1);
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
    for (int i = 2; i < BufferCount(); ++i) {
        tri[(*indexCount)++] = Buffer()[0];
        tri[(*indexCount)++] = Buffer()[i - 1];
        tri[(*indexCount)++] = Buffer()[i];
    }
    return 1;
}
EI_REPLACE(0x0042D910, ClipTriangle);

} // namespace clip
