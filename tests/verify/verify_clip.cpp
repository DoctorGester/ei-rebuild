// Differential checks for clip.cpp.
//
// LerpVertex is pure: ours writes a scratch vertex first, then the original the real one.
// The clippers change their inputs (index list, counts), append vertices and fill the global work
// buffer: ours runs on the real state first and its results are recorded, then the inputs (and the
// last intersection parameter) are restored and the original runs; the results must be identical
// (counts, index lists, the appended vertices bit for bit, the work buffer and the parameter).
#include "verify.h"

#include "../../src/game/clip.h"

#include <cstring>

namespace {

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// 0042E4D0: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x18 (3).
void(__fastcall* s_lerp)(const TLVertex*, const TLVertex*, float, TLVertex*);
void __fastcall LerpVerify(const TLVertex* a, const TLVertex* b, float t, TLVertex* out) {
    TLVertex actual;
    clip::LerpVertex(a, b, t, &actual);
    s_lerp(a, b, t, out);
    ei::verify::Check("clip::LerpVertex", true, std::memcmp(out, &actual, sizeof(actual)) == 0,
                      "t %08X colours %08X %08X", Bits(t), a->color, b->color);
}

constexpr int kMaxIndices = 64;
constexpr int kMaxVertices = 64;

// What a clip call produced.
struct Outcome {
    int result;
    int vertexCount;
    int count;
    int bufferCount;
    std::uint32_t ratio;
    std::uint16_t indices[kMaxIndices];
    std::uint16_t buffer[kMaxIndices];
    TLVertex vertices[kMaxVertices];

    void Capture(int r, const TLVertex* v, int firstVertex, int vc, const std::uint16_t* idx, int n) {
        std::memset(this, 0, sizeof(*this));
        result = r;
        vertexCount = vc;
        count = n;
        bufferCount = clip::BufferCount();
        ratio = Bits(clip::LastRatio());
        if (n > 0 && n <= kMaxIndices) std::memcpy(indices, idx, n * sizeof(std::uint16_t));
        if (bufferCount > 0 && bufferCount <= kMaxIndices)
            std::memcpy(buffer, clip::Buffer(), bufferCount * sizeof(std::uint16_t));
        int added = vc - firstVertex;
        if (added > 0 && added <= kMaxVertices) std::memcpy(vertices, v + firstVertex, added * sizeof(TLVertex));
    }
};

bool Same(const Outcome& a, const Outcome& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

using PlaneFn = int(__fastcall*)(int, TLVertex*, int*, std::uint16_t*, int*, float);

int CheckPlane(const char* name, PlaneFn original, PlaneFn ours, int outcode, TLVertex* v, int* vertexCount,
               std::uint16_t* indices, int* count, float limit) {
    const int vc0 = *vertexCount;
    const int n0 = *count;
    if (n0 < 0 || n0 > kMaxIndices) return ours(outcode, v, vertexCount, indices, count, limit);
    std::uint16_t input[kMaxIndices];
    std::memcpy(input, indices, n0 * sizeof(std::uint16_t));
    const float ratio0 = clip::LastRatio();

    static Outcome actual, expected;   // large; the clippers do not recurse
    int r = ours(outcode, v, vertexCount, indices, count, limit);
    actual.Capture(r, v, vc0, *vertexCount, indices, *count);

    *vertexCount = vc0;
    *count = n0;
    std::memcpy(indices, input, n0 * sizeof(std::uint16_t));
    clip::LastRatio() = ratio0;
    r = original(outcode, v, vertexCount, indices, count, limit);
    expected.Capture(r, v, vc0, *vertexCount, indices, *count);

    ei::verify::Check(name, true, Same(expected, actual), "n %d -> %d / %d, vertices %d -> %d / %d, limit %08X",
                      n0, expected.count, actual.count, vc0, expected.vertexCount, actual.vertexCount, Bits(limit));
    return r;
}

// All six: PUSH ECX (1), MOV ECX,[ESP+0x10] (4).
PlaneFn s_xBelow, s_xAbove, s_yBelow, s_yAbove, s_zBelow, s_zAbove;
int __fastcall XBelowVerify(int o, TLVertex* v, int* vc, std::uint16_t* i, int* n, float l) {
    return CheckPlane("clip::ClipXBelow", s_xBelow, clip::ClipXBelow, o, v, vc, i, n, l);
}
int __fastcall XAboveVerify(int o, TLVertex* v, int* vc, std::uint16_t* i, int* n, float l) {
    return CheckPlane("clip::ClipXAbove", s_xAbove, clip::ClipXAbove, o, v, vc, i, n, l);
}
int __fastcall YBelowVerify(int o, TLVertex* v, int* vc, std::uint16_t* i, int* n, float l) {
    return CheckPlane("clip::ClipYBelow", s_yBelow, clip::ClipYBelow, o, v, vc, i, n, l);
}
int __fastcall YAboveVerify(int o, TLVertex* v, int* vc, std::uint16_t* i, int* n, float l) {
    return CheckPlane("clip::ClipYAbove", s_yAbove, clip::ClipYAbove, o, v, vc, i, n, l);
}
int __fastcall ZBelowVerify(int o, TLVertex* v, int* vc, std::uint16_t* i, int* n, float l) {
    return CheckPlane("clip::ClipZBelow", s_zBelow, clip::ClipZBelow, o, v, vc, i, n, l);
}
int __fastcall ZAboveVerify(int o, TLVertex* v, int* vc, std::uint16_t* i, int* n, float l) {
    return CheckPlane("clip::ClipZAbove", s_zAbove, clip::ClipZAbove, o, v, vc, i, n, l);
}

// 0042D910: PUSH ECX, PUSH EBX, PUSH EBP, PUSH ESI, PUSH EDI (5). The original calls the clippers
// through their (hooked) addresses, so their wrappers run inside it.
int(__fastcall* s_triangle)(TLVertex*, const std::uint8_t*, std::uint16_t*, int*, int*, float, float);
int __fastcall TriangleVerify(TLVertex* v, const std::uint8_t* outcodes, std::uint16_t* tri, int* vertexCount,
                              int* indexCount, float xMax, float yMax) {
    const int vc0 = *vertexCount;
    const int n0 = *indexCount;
    std::uint16_t input[3] = {tri[0], tri[1], tri[2]};
    const float ratio0 = clip::LastRatio();

    static Outcome actual, expected;
    int r = clip::ClipTriangle(v, outcodes, tri, vertexCount, indexCount, xMax, yMax);
    actual.Capture(r, v, vc0, *vertexCount, tri, *indexCount);

    *vertexCount = vc0;
    *indexCount = n0;
    std::memcpy(tri, input, sizeof(input));
    clip::LastRatio() = ratio0;
    r = s_triangle(v, outcodes, tri, vertexCount, indexCount, xMax, yMax);
    expected.Capture(r, v, vc0, *vertexCount, tri, *indexCount);

    ei::verify::Check("clip::ClipTriangle", true, Same(expected, actual),
                      "result %d / %d, indices %d / %d, vertices %d -> %d / %d, outcodes %02X %02X %02X", expected.result,
                      actual.result, expected.count, actual.count, vc0, expected.vertexCount, actual.vertexCount,
                      outcodes[input[0]], outcodes[input[1]], outcodes[input[2]]);
    return r;
}

} // namespace

EI_VERIFY(0x0042E4D0, LerpVerify, 6, s_lerp);
EI_VERIFY(0x0042DB70, XBelowVerify, 5, s_xBelow);
EI_VERIFY(0x0042DD00, XAboveVerify, 5, s_xAbove);
EI_VERIFY(0x0042DE90, YBelowVerify, 5, s_yBelow);
EI_VERIFY(0x0042E020, YAboveVerify, 5, s_yAbove);
EI_VERIFY(0x0042E1B0, ZBelowVerify, 5, s_zBelow);
EI_VERIFY(0x0042E340, ZAboveVerify, 5, s_zAbove);
EI_VERIFY(0x0042D910, TriangleVerify, 5, s_triangle);
