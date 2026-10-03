// Figure mesh loaders (005A0000-005B0000): the bounding volume and the FIG mesh data of a figure
// part, morphed by the figure's three morph coefficients. See docs/notes/world.md ("Figure
// meshes").
#include "figure_mesh.h"

#include "core_maths.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstring>

namespace {

// 00500960: the display object (007AF060); +8 -> +0xC is the 3D library's transform pipeline.
__declspec(naked) void* __cdecl Display() EI_ORIGINAL(0x00500960)

// Slot 7 of the transform pipeline (in 3DfpFPU.dll): fits `bounds` to the vertices.
void FitBounds(ClipBounds* bounds, MeshVertex* vertices, int count) {
    char* display = static_cast<char*>(Display());
    void* pipeline = *reinterpret_cast<void**>(*reinterpret_cast<char**>(display + 8) + 0xC);
    ei::VCall<void>(pipeline, 7, bounds, vertices, count);
}

template <typename T>
T Read(char*& p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return v;
}

} // namespace

// The arithmetic below is compiled exactly as written: /fp:fast must not regroup it, and each value
// the original stores to a float goes through StoreFloat (F); everything else stays on the FPU.
#pragma float_control(precise, on, push)

namespace {

inline float F(double v) { return StoreFloat(static_cast<float>(v)); }

// Inline Vector3 arithmetic of the original: every component of every operation is rounded to a
// float (each result goes through a Vector3 temporary).
struct V3 {
    float x, y, z;
};
V3 Load(const float* p) { return {p[0], p[1], p[2]}; }
V3 Sub(const V3& a, const V3& b) { return {F(a.x - b.x), F(a.y - b.y), F(a.z - b.z)}; }
V3 Add(const V3& a, const V3& b) { return {F(a.x + b.x), F(a.y + b.y), F(a.z + b.z)}; }
V3 Scale(const V3& a, float s) { return {F(a.x * s), F(a.y * s), F(a.z * s)}; }
// a + (b - a) * s, written (b - a) * s + a as the original evaluates it.
V3 Lerp(const V3& a, const V3& b, float s) { return Add(Scale(Sub(b, a), s), a); }

// One morphed vector from 8 corner versions (`v` points at 8 Vector3s):
//   lerp(lerp(lerp(v0, v1, s), lerp(v2, v3, s), t), lerp(lerp(v4, v5, s), lerp(v6, v7, s), t), u)
// `keepZ`: the original keeps the z component of the upper blend unrounded on the FPU for the
// final difference (centre and box minimum; FST then FSUB), while the box maximum goes through
// the out-of-line Vector3 helpers and rounds it.
V3 MorphVector(const float* v, float s, float t, float u, bool keepZ) {
    V3 lower = Lerp(Lerp(Load(v), Load(v + 3), s), Lerp(Load(v + 6), Load(v + 9), s), t);
    V3 a = Lerp(Load(v + 12), Load(v + 15), s);
    V3 b = Lerp(Load(v + 18), Load(v + 21), s);
    V3 step = Scale(Sub(b, a), t);
    V3 diff;
    if (keepZ) {
        diff = {F(F(step.x + a.x) - lower.x), F(F(step.y + a.y) - lower.y), F((static_cast<double>(step.z) + a.z) - lower.z)};
    } else {
        diff = Sub(Add(step, a), lower);
    }
    return Add(Scale(diff, u), lower);
}

// The older 6-corner layout (non-FIG files), as for the bones (figure.h, BoneMorphBox):
//   lerp(lerp(lerp(c0, c1, s), c2, t), lerp(lerp(c3, c4, s), c5, t), u)
// with the same unrounded upper z when `keepZ`.
V3 MorphVector6(const float* c, float s, float t, float u, bool keepZ) {
    V3 lower = Lerp(Lerp(Load(c), Load(c + 3), s), Load(c + 6), t);
    V3 a = Lerp(Load(c + 9), Load(c + 12), s);
    V3 step = Scale(Sub(Load(c + 15), a), t);
    V3 diff;
    if (keepZ) {
        diff = {F(F(step.x + a.x) - lower.x), F(F(step.y + a.y) - lower.y), F((static_cast<double>(step.z) + a.z) - lower.z)};
    } else {
        diff = Sub(Add(step, a), lower);
    }
    return Add(Scale(diff, u), lower);
}

// One component of a 6-corner value (corner k at c[3 * k]), in the four roundings the original's
// register allocation produced in the old-format loaders (each was read off the instruction
// stream; see the notes):
//   PerOp: every operation rounded (the inlined Vector3 operators).
//   Kept:  the two s-blends stored, everything else on the FPU.
//   StoredLower: s-blends and the lower t-blend stored; the upper t-step stored but added on the
//          FPU; then per operation.
//   PerOpKeptSum: per operation, except the upper sum b + step, which is not stored.
//   PerOpKeptUpper: as PerOpKeptSum, and the upper step's difference (c5 - b) stays on the FPU.
float LerpP(float a, float b, float s) { return F(F(F(b - a) * s) + a); }

float CornerPerOp(const float* c, float s, float t, float u) {
    return LerpP(LerpP(LerpP(c[0], c[3], s), c[6], t), LerpP(LerpP(c[9], c[12], s), c[15], t), u);
}
float CornerKept(const float* c, float s, float t, float u) {
    float a = F((static_cast<double>(c[3]) - c[0]) * s + c[0]);
    double lower = (static_cast<double>(c[6]) - a) * t + a;
    float b = F((static_cast<double>(c[12]) - c[9]) * s + c[9]);
    double upper = (static_cast<double>(c[15]) - b) * t + b;
    return F((upper - lower) * u + lower);
}
float CornerStoredLower(const float* c, float s, float t, float u) {
    float a = F((static_cast<double>(c[3]) - c[0]) * s + c[0]);
    float lower = F((static_cast<double>(c[6]) - a) * t + a);
    float b = F((static_cast<double>(c[12]) - c[9]) * s + c[9]);
    float step = F((static_cast<double>(c[15]) - b) * t);
    float diff = F((static_cast<double>(b) + step) - lower);
    return F(F(diff * u) + lower);
}
float CornerPerOpKeptSum(const float* c, float s, float t, float u) {
    float lower = LerpP(LerpP(c[0], c[3], s), c[6], t);
    float b = LerpP(c[9], c[12], s);
    float step = F(F(c[15] - b) * t);
    float diff = F((static_cast<double>(b) + step) - lower);
    return F(F(diff * u) + lower);
}
float CornerPerOpKeptUpper(const float* c, float s, float t, float u) {
    float lower = LerpP(LerpP(c[0], c[3], s), c[6], t);
    float b = LerpP(c[9], c[12], s);
    float step = F((static_cast<double>(c[15]) - b) * t);
    float diff = F((static_cast<double>(step) + b) - lower);
    return F(F(diff * u) + lower);
}

// A scalar from 6 corners, kept on the FPU until the result.
float MorphScalar6(const float* r, float s, float t, float u) {
    double a = (static_cast<double>(r[1]) - r[0]) * s + r[0];
    double lower = (r[2] - a) * t + a;
    double b = (static_cast<double>(r[4]) - r[3]) * s + r[3];
    double upper = (r[5] - b) * t + b;
    return F((upper - lower) * u + lower);
}

// A scalar morphed from 8 versions. The inner blends stay on the FPU; 005A18E0 stores the lower
// half (rounded) before the final blend, 005AAE60 keeps it.
float MorphScalar(const float* r, float s, float t, float u, bool storeLower) {
    double a = (static_cast<double>(r[1]) - r[0]) * s + r[0];
    double b = (static_cast<double>(r[3]) - r[2]) * s + r[2];
    double lower = (b - a) * t + a;
    if (storeLower) lower = F(lower);
    double c = (static_cast<double>(r[5]) - r[4]) * s + r[4];
    double d = (static_cast<double>(r[7]) - r[6]) * s + r[6];
    double upper = (d - c) * t + c;
    return F((upper - lower) * u + lower);
}

} // namespace

void CFigureMesh::LoadBounds(char** cursor, float s, float t, float u, std::uint32_t magic) {
    if (magic == kFig8Magic) {
        const float* p = reinterpret_cast<const float*>(*cursor);
        V3 c = MorphVector(p, s, t, u, true);
        center_ = {c.x, c.y, c.z};
        *cursor += 0x60;
        p = reinterpret_cast<const float*>(*cursor);
        V3 mn = MorphVector(p, s, t, u, true);
        boxMin_ = {mn.x, mn.y, mn.z};
        *cursor += 0x60;
        p = reinterpret_cast<const float*>(*cursor);
        V3 mx = MorphVector(p, s, t, u, false);
        boxMax_ = {mx.x, mx.y, mx.z};
        *cursor += 0x60;
        radius_ = MorphScalar(reinterpret_cast<const float*>(*cursor), s, t, u, true);
        *cursor += 0x20;

        std::memcpy(bounds_.center, &center_, 12);
        bounds_.radius = radius_;
        bounds_.flags |= 1;
        std::memcpy(bounds_.boxMin, &boxMin_, 12);
        std::memcpy(bounds_.boxMax, &boxMax_, 12);
        bounds_.flags |= 2;
        return;
    }
    std::memcpy(&center_, *cursor, 12);
    *cursor += 12;
    std::memcpy(&boxMin_, *cursor, 12);
    *cursor += 12;
    std::memcpy(&boxMax_, *cursor, 12);
    *cursor += 12;
    std::memcpy(&radius_, *cursor, 4);
    *cursor += 4;
    std::memcpy(bounds_.center, &center_, 12);
    bounds_.flags |= 1;
    bounds_.radius = radius_;
    bounds_.SetBox(&boxMin_.x, &boxMax_.x);
}
EI_REPLACE_METHOD(0x005A18E0, CFigureMesh::LoadBounds);

void __stdcall MorphFigureBounds(ClipBounds* out, const float* data, float s, float t, float u, std::uint32_t magic) {
    Vector3 boxMin, boxMax;
    if (magic == kFig8Magic) {
        V3 c = MorphVector(data, s, t, u, true);
        V3 mn = MorphVector(data + 0x18, s, t, u, true);
        V3 mx = MorphVector(data + 0x30, s, t, u, false);
        float radius = MorphScalar(data + 0x48, s, t, u, false);
        out->flags |= 1;
        out->center[0] = c.x;
        out->center[1] = c.y;
        out->center[2] = c.z;
        out->radius = radius;
        boxMin = {mn.x, mn.y, mn.z};
        boxMax = {mx.x, mx.y, mx.z};
    } else {
        std::memcpy(out->center, data, 12);
        std::memcpy(&boxMin, data + 3, 12);
        std::memcpy(&boxMax, data + 6, 12);
        out->radius = data[9];
        out->flags |= 1;
    }
    out->SetBox(&boxMin.x, &boxMax.x);
}
EI_REPLACE(0x005AAE60, MorphFigureBounds);

void __stdcall MorphFigureBounds6(ClipBounds* out, const float* data, float s, float t, float u) {
    V3 c = MorphVector6(data, s, t, u, true);
    V3 mn = MorphVector6(data + 0x12, s, t, u, false);
    V3 mx = MorphVector6(data + 0x24, s, t, u, false);
    float radius = MorphScalar6(data + 0x36, s, t, u);
    out->center[0] = c.x;
    out->center[1] = c.y;
    out->center[2] = c.z;
    out->flags |= 1;
    out->radius = radius;
    Vector3 boxMin{mn.x, mn.y, mn.z};
    Vector3 boxMax{mx.x, mx.y, mx.z};
    out->SetBox(&boxMin.x, &boxMax.x);
}
EI_REPLACE(0x005AB9A0, MorphFigureBounds6);

namespace {

// One 16-byte block of a FIG8 vertex: `v` holds the block's 8 corner versions (8 x 4 floats).
// The x component stays on the FPU through every blend; y, z and w are stored after each one.
__declspec(noinline) void MorphBlock(float* out, const float* v, float s, float t, float u) {
    // Lower half (versions 0-3), written to `out`.
    {
        double ax = (static_cast<double>(v[4]) - v[0]) * s + v[0];
        float ay = F((static_cast<double>(v[5]) - v[1]) * s + v[1]);
        float az = F((static_cast<double>(v[6]) - v[2]) * s + v[2]);
        float aw = F((static_cast<double>(v[7]) - v[3]) * s + v[3]);
        double bx = (static_cast<double>(v[12]) - v[8]) * s + v[8];
        float by = F((static_cast<double>(v[13]) - v[9]) * s + v[9]);
        float bz = F((static_cast<double>(v[14]) - v[10]) * s + v[10]);
        float bw = F((static_cast<double>(v[15]) - v[11]) * s + v[11]);
        out[0] = F((bx - ax) * t + ax);
        out[1] = F((static_cast<double>(by) - ay) * t + ay);
        out[2] = F((static_cast<double>(bz) - az) * t + az);
        out[3] = F((static_cast<double>(bw) - aw) * t + aw);
    }
    // Upper half (versions 4-7), blended into `out` by u.
    {
        double ax = (static_cast<double>(v[20]) - v[16]) * s + v[16];
        float ay = F((static_cast<double>(v[21]) - v[17]) * s + v[17]);
        float az = F((static_cast<double>(v[22]) - v[18]) * s + v[18]);
        float aw = F((static_cast<double>(v[23]) - v[19]) * s + v[19]);
        double bx = (static_cast<double>(v[28]) - v[24]) * s + v[24];
        float by = F((static_cast<double>(v[29]) - v[25]) * s + v[25]);
        float bz = F((static_cast<double>(v[30]) - v[26]) * s + v[26]);
        float bw = F((static_cast<double>(v[31]) - v[27]) * s + v[27]);
        double cx = (bx - ax) * t + ax;
        float cy = F((static_cast<double>(by) - ay) * t + ay);
        float cz = F((static_cast<double>(bz) - az) * t + az);
        float cw = F((static_cast<double>(bw) - aw) * t + aw);
        out[0] = F((cx - out[0]) * u + out[0]);
        out[1] = F((static_cast<double>(cy) - out[1]) * u + out[1]);
        out[2] = F((static_cast<double>(cz) - out[2]) * u + out[2]);
        out[3] = F((static_cast<double>(cw) - out[3]) * u + out[3]);
    }
}

// One 16-byte block of an old-format vertex: 6 corner versions (6 x 4 floats), every blend on
// the FPU.
__declspec(noinline) void MorphBlock6(float* out, const float* v, float s, float t, float u) {
    for (int k = 0; k < 4; ++k) {
        double a = (static_cast<double>(v[4 + k]) - v[k]) * s + v[k];
        double lower = (v[8 + k] - a) * t + a;
        double b = (static_cast<double>(v[16 + k]) - v[12 + k]) * s + v[12 + k];
        double upper = (v[20 + k] - b) * t + b;
        out[k] = F((upper - lower) * u + lower);
    }
}

using CornerFn = float (*)(const float*, float, float, float);

// The old-format bounding volume (centre, box minimum, box maximum: 6 corners of 3 floats each;
// radius: 6 floats), written straight into `bounds`. The two loaders round the x component and
// the box minimum differently.
void MorphOldBounds(ClipBounds& bounds, const float* p, float s, float t, float u, CornerFn minX, CornerFn minYZ) {
    float c[3] = {CornerKept(p, s, t, u), CornerPerOp(p + 1, s, t, u), CornerPerOp(p + 2, s, t, u)};
    const float* q = p + 0x12;
    float mn[3] = {minX(q, s, t, u), minYZ(q + 1, s, t, u), minYZ(q + 2, s, t, u)};
    q = p + 0x24;
    float mx[3] = {CornerPerOpKeptSum(q, s, t, u), CornerPerOpKeptSum(q + 1, s, t, u), CornerPerOpKeptSum(q + 2, s, t, u)};
    float radius = MorphScalar6(p + 0x36, s, t, u);
    std::memcpy(bounds.center, c, 12);
    bounds.flags |= 1;
    bounds.radius = radius;
    std::memcpy(bounds.boxMin, mn, 12);
    std::memcpy(bounds.boxMax, mx, 12);
    bounds.flags |= 2;
}

} // namespace

#pragma float_control(pop)

namespace {

// The vertices of a FIG mesh: FIG8 files hold 8 versions of each of the three blocks (0x80 bytes
// each), morphed; other versions hold the three blocks once. The fourth block is set to 1.0.
void LoadVertices(AlignedArray<MeshVertex>& vertices, char** cursor, std::uint32_t magic, float s, float t, float u) {
    const std::uint32_t one = 0x3F800000;
    if (magic == kFig8Magic) {
        for (int i = 0; i < vertices.size; ++i) {
            MeshVertex& v = vertices.data[i];
            for (int b = 0; b < 3; ++b) {
                MorphBlock(v.block[b], reinterpret_cast<const float*>(*cursor), s, t, u);
                *cursor += 0x80;
            }
            for (int k = 3; k >= 0; --k) std::memcpy(&v.block[3][k], &one, 4);
        }
        return;
    }
    for (int i = 0; i < vertices.size; ++i) {
        MeshVertex& v = vertices.data[i];
        for (int b = 0; b < 3; ++b) {
            std::memcpy(v.block[b], *cursor, 16);
            *cursor += 0x10;
        }
        for (int k = 3; k >= 0; --k) std::memcpy(&v.block[3][k], &one, 4);
    }
}

// Sizes `array` to `count` and copies `count` elements from the cursor.
template <typename T>
void LoadArray(AlignedArray<T>& array, char** cursor, int count) {
    array.SetSize(count);
    if (count != 0) {
        std::memcpy(array.data, *cursor, static_cast<std::size_t>(count) * sizeof(T));
        *cursor += count * static_cast<int>(sizeof(T));
    }
}

} // namespace

void CFigureMesh::LoadFig(char** cursor, float s, float t, float u) {
    if (flags_ & 0x20) return;
    FigHeader header;
    std::memcpy(&header, *cursor, sizeof(header));
    *cursor += sizeof(header);
    unknown_158 = Read<std::uint32_t>(*cursor);
    unknown_15C = Read<std::uint32_t>(*cursor);
    // 005A18E0 through its address (the verify build checks it there).
    reinterpret_cast<void(__fastcall*)(CFigureMesh*, void*, char**, float, float, float, std::uint32_t)>(0x005A18E0)(
        this, nullptr, cursor, s, t, u, header.magic);

    vertices_.SetSize(header.vertexCount);
    LoadVertices(vertices_, cursor, header.magic, s, t, u);
    if (vertices_.size != 0) FitBounds(&bounds_, vertices_.data, vertices_.size);

    records_.SetSize(header.recordCount);
    colorsA_.SetSize(header.recordCount * 4);
    colorsB_.SetSize(header.recordCount * 4);
    if (header.recordCount != 0) {
        std::memcpy(records_.data, *cursor, static_cast<std::size_t>(header.recordCount) * sizeof(MeshRecord40));
        *cursor += header.recordCount * 0x40;
    }
    LoadArray(pairs_, cursor, header.pairCount);
    LoadArray(shorts_, cursor, header.shortCount);
    LoadArray(triangles_, cursor, header.triangleCount);
    LoadArray(words_, cursor, header.wordCount);

    buffers_.vertices = vertices_.data;
    buffers_.vertexCount = vertices_.size;
    buffers_.records = records_.data;
    buffers_.recordCount = records_.size;
    colorsA_.SetSize(records_.size * 4);   // 005A6E70 with growBy -1
    colorsB_.SetSize(records_.size * 4);
    buffers_.pairs = pairs_.data;
    buffers_.pairCount = pairs_.size;
    buffers_.shorts = shorts_.data;
    buffers_.shortCount = shorts_.size;
    buffers_.triangles = triangles_.data;
    buffers_.triangleCount = triangles_.size;
    buffers_.wordCount = words_.size;
    buffers_.words = words_.data;
    flags_ |= 0x20;
}
EI_REPLACE_METHOD(0x005A2500, CFigureMesh::LoadFig);

void CFigureMesh::LoadFigShared(char** cursor, float s, float t, float u) {
    if (flags_ & 0x20) return;
    FigHeader header;
    std::memcpy(&header, *cursor, sizeof(header));
    *cursor += sizeof(header);
    unknown_158 = Read<std::uint32_t>(*cursor);
    unknown_15C = Read<std::uint32_t>(*cursor);
    reinterpret_cast<void(__fastcall*)(CFigureMesh*, void*, char**, float, float, float, std::uint32_t)>(0x005A18E0)(
        this, nullptr, cursor, s, t, u, header.magic);

    vertices_.SetSize(header.vertexCount);
    LoadVertices(vertices_, cursor, header.magic, s, t, u);
    if (vertices_.size != 0) FitBounds(&bounds_, vertices_.data, vertices_.size);

    records_.SetSize(header.recordCount);
    colorsA_.SetSize(header.recordCount * 4);
    colorsB_.SetSize(header.recordCount * 4);
    if (header.recordCount != 0) {
        std::memcpy(records_.data, *cursor, static_cast<std::size_t>(header.recordCount) * sizeof(MeshRecord40));
        *cursor += header.recordCount * 0x40;
    }
    // The rest stays in the file data.
    buffers_.pairs = *cursor;
    buffers_.pairCount = header.pairCount;
    *cursor += header.pairCount * 8;
    buffers_.shorts = *cursor;
    buffers_.shortCount = header.shortCount;
    *cursor += header.shortCount * 2;
    buffers_.triangles = *cursor;
    buffers_.triangleCount = header.triangleCount;
    *cursor += header.triangleCount * 6;
    buffers_.words = *cursor;
    buffers_.wordCount = header.wordCount;
    *cursor += header.wordCount * 4;

    buffers_.vertices = vertices_.data;
    buffers_.vertexCount = vertices_.size;
    buffers_.records = records_.data;
    buffers_.recordCount = records_.size;
    colorsA_.SetSize(records_.size * 4);
    colorsB_.SetSize(records_.size * 4);
    flags_ |= 0x20;
}
EI_REPLACE_METHOD(0x005A5C70, CFigureMesh::LoadFigShared);

// ---- Old (non-FIG) format -----------------------------------------------------------------------

namespace {

// Reads the count in front of a block.
int ReadCount(char** cursor) {
    int n;
    std::memcpy(&n, *cursor, 4);
    *cursor += 4;
    return n;
}

// The part both old-format loaders share: header words, bounds, vertices and records (the
// records' second colour array is set to opaque black).
void LoadOldCommon(CFigureMesh* mesh, char** cursor, float s, float t, float u, bool shared) {
    mesh->unknown_158 = Read<std::uint32_t>(*cursor);
    mesh->unknown_15C = Read<std::uint32_t>(*cursor);
    if (shared)
        MorphOldBounds(mesh->bounds_, reinterpret_cast<const float*>(*cursor), s, t, u, CornerKept, CornerPerOp);
    else
        MorphOldBounds(mesh->bounds_, reinterpret_cast<const float*>(*cursor), s, t, u, CornerStoredLower, CornerPerOpKeptUpper);
    *cursor += 0xF0;

    int count = ReadCount(cursor);
    mesh->vertices_.SetSize(count);
    for (int i = 0; i < count; ++i) {
        MeshVertex& v = mesh->vertices_.data[i];
        for (int b = 0; b < 3; ++b) {
            MorphBlock6(v.block[b], reinterpret_cast<const float*>(*cursor), s, t, u);
            *cursor += 0x60;
        }
        const std::uint32_t one = 0x3F800000;
        for (int k = 3; k >= 0; --k) std::memcpy(&v.block[3][k], &one, 4);
    }
    if (mesh->vertices_.size != 0) FitBounds(&mesh->bounds_, mesh->vertices_.data, mesh->vertices_.size);

    count = ReadCount(cursor);
    mesh->records_.SetSize(count);
    mesh->colorsA_.SetSize(count * 4);
    mesh->colorsB_.SetSize(count * 4);
    if (count != 0) {
        std::memcpy(mesh->records_.data, *cursor, static_cast<std::size_t>(count) * sizeof(MeshRecord40));
        *cursor += count * 0x40;
        for (int i = 0; i < mesh->colorsB_.size; ++i) mesh->colorsB_.data[i] = 0xFF000000;
    }
}

// A block of 0x34-byte records the loaders skip.
void SkipRecords34(char** cursor) {
    int n = ReadCount(cursor);
    *cursor += n * 0x34;
}

} // namespace

void CFigureMesh::LoadOld(char** cursor, float s, float t, float u) {
    if (flags_ & 0x20) return;
    LoadOldCommon(this, cursor, s, t, u, false);
    LoadArray(pairs_, cursor, ReadCount(cursor));
    SkipRecords34(cursor);
    LoadArray(triangles_, cursor, ReadCount(cursor));
    LoadArray(words_, cursor, ReadCount(cursor));
    LoadArray(shorts_, cursor, ReadCount(cursor));   // 0044FA90 with growBy -1

    buffers_.vertices = vertices_.data;
    buffers_.vertexCount = vertices_.size;
    buffers_.records = records_.data;
    buffers_.recordCount = records_.size;
    colorsA_.SetSize(records_.size * 4);
    colorsB_.SetSize(records_.size * 4);
    buffers_.pairs = pairs_.data;
    buffers_.pairCount = pairs_.size;
    buffers_.triangles = triangles_.data;
    buffers_.triangleCount = triangles_.size;
    buffers_.words = words_.data;
    buffers_.wordCount = words_.size;
    buffers_.shortCount = shorts_.size;
    buffers_.shorts = shorts_.data;
    flags_ |= 0x20;
}
EI_REPLACE_METHOD(0x005A3800, CFigureMesh::LoadOld);

void CFigureMesh::LoadOldShared(char** cursor, float s, float t, float u) {
    // Unlike the other three loaders, this one does not test the loaded flag.
    LoadOldCommon(this, cursor, s, t, u, true);
    // The rest stays in the file data.
    buffers_.pairCount = ReadCount(cursor);
    buffers_.pairs = *cursor;
    *cursor += buffers_.pairCount * 8;
    SkipRecords34(cursor);
    buffers_.triangleCount = ReadCount(cursor);
    buffers_.triangles = *cursor;
    *cursor += buffers_.triangleCount * 6;
    buffers_.wordCount = ReadCount(cursor);
    buffers_.words = *cursor;
    *cursor += buffers_.wordCount * 4;
    buffers_.shortCount = ReadCount(cursor);
    buffers_.shorts = *cursor;
    *cursor += buffers_.shortCount * 2;

    buffers_.vertices = vertices_.data;
    buffers_.vertexCount = vertices_.size;
    buffers_.records = records_.data;
    buffers_.recordCount = records_.size;
    colorsA_.SetSize(records_.size * 4);
    colorsB_.SetSize(records_.size * 4);
    flags_ |= 0x20;
}
EI_REPLACE_METHOD(0x005A4D60, CFigureMesh::LoadOldShared);

// ---- World-space box ----------------------------------------------------------------------------

namespace {
// 00401570 (not rewritten): the object-to-world matrix of a transform object (rotation from the
// quaternion at +0x34, translation = position) into out[16]. __thiscall, RET 4.
__declspec(naked) void __fastcall ObjectMatrix(const void* /*object*/, void*, float* /*out*/) EI_ORIGINAL(0x00401570)

// The limit on the squared distance of the position from the origin (the float 1e8 at 0073FAFC).
inline float FarLimit() { return *reinterpret_cast<const float*>(0x0073FAFC); }
} // namespace

#pragma float_control(precise, on, push)

namespace {
// Grows [min, max] by one transformed point. The original selects with the FCOMP status bit C0
// ("less or unordered") as a mask on the float bits, so a NaN on either side takes the point.
void Extend(float& max, float& min, float v) {
    if (!(max >= v)) max = v;
    if (!(v >= min)) min = v;
}
} // namespace

void CFigureMesh::ExtendWorldBox(float* maxX, float* maxY, float* maxZ, float* minX, float* minY, float* minZ) const {
    // ((x*x + y*y) + z*z) of the position, on the FPU; NaN passes.
    double far = static_cast<double>(position_[0]) * position_[0] + static_cast<double>(position_[1]) * position_[1];
    far = far + static_cast<double>(position_[2]) * position_[2];
    if (far > FarLimit()) return;

    float m[16];
    std::memset(m, 0, sizeof(m));
    ObjectMatrix(this, nullptr, m);

    // The 8 corners of the box (centre + minimum / maximum), maximum x first; each sum stays on
    // the FPU, each transformed coordinate is stored.
    const float* c = bounds_.center;
    const float* lo = bounds_.boxMin;
    const float* hi = bounds_.boxMax;
    for (int corner = 0; corner < 8; ++corner) {
        const float* sx = (corner & 4) ? lo : hi;
        const float* sy = (corner & 2) ? lo : hi;
        const float* sz = (corner & 1) ? lo : hi;
        double x = static_cast<double>(sx[0]) + c[0];
        double y = static_cast<double>(sy[1]) + c[1];
        double z = static_cast<double>(sz[2]) + c[2];
        float wx = F(((z * m[8] + y * m[4]) + x * m[0]) + m[12]);
        float wy = F(((z * m[9] + y * m[5]) + x * m[1]) + m[13]);
        float wz = F(((z * m[10] + y * m[6]) + x * m[2]) + m[14]);
        Extend(*maxX, *minX, wx);
        Extend(*maxY, *minY, wy);
        Extend(*maxZ, *minZ, wz);
    }
}
EI_REPLACE_METHOD(0x0059FDF0, CFigureMesh::ExtendWorldBox);

#pragma float_control(pop)
