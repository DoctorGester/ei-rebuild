// Figure mesh loaders (005A0000-005B0000): the bounding volume and the FIG mesh data of a figure
// part, morphed by the figure's three morph coefficients. See docs/notes/world.md ("Figure
// meshes").
#include "core_transform.h"
#include "figure_mesh.h"

#include "core_maths.h"

#include "link_figure.h"   // ITransformPipeline

#include "ei/hook.h"
#include "model.h"
#include "originals_render.h"
#include "render/render.h"   // MeshChanged
#include "game_data.h"

#include <cstring>

namespace {

// Slot 7 of the scene renderer's (00500960, the global at 007AF060) transform object (in
// 3DfpFPU.dll): fits `bounds` to the vertices.
void FitBounds(ClipBounds* bounds, VertexPacket* vertices, int count) {
    auto* renderer = SceneRenderer();
    renderer->pipeline_->transform_->FitBounds(bounds, vertices, count);
}

template <typename T>
T Read(char*& p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return v;
}

} // namespace

// Each value the original stores to a float is rounded with static_cast<float>; everything else
// stays on the FPU.

namespace {

// Inline Vector3 arithmetic of the original: every component of every operation is rounded to a
// float (each result goes through a Vector3 temporary).
Vector3 Load(const float* p) { return {p[0], p[1], p[2]}; }
Vector3 Sub(const Vector3& a, const Vector3& b) { return {static_cast<float>(a.x - b.x), static_cast<float>(a.y - b.y), static_cast<float>(a.z - b.z)}; }
Vector3 Add(const Vector3& a, const Vector3& b) { return {static_cast<float>(a.x + b.x), static_cast<float>(a.y + b.y), static_cast<float>(a.z + b.z)}; }
Vector3 Scale(const Vector3& a, float s) { return {static_cast<float>(a.x * s), static_cast<float>(a.y * s), static_cast<float>(a.z * s)}; }
// a + (b - a) * s, written (b - a) * s + a as the original evaluates it.
Vector3 LerpStored(const Vector3& a, const Vector3& b, float s) { return Add(Scale(Sub(b, a), s), a); }

// One morphed vector from 8 corner versions (`v` points at 8 Vector3s):
//   lerp(lerp(lerp(v0, v1, s), lerp(v2, v3, s), t), lerp(lerp(v4, v5, s), lerp(v6, v7, s), t), u)
// `keepZ`: the original keeps the z component of the upper blend unrounded on the FPU for the
// final difference (centre and box minimum; FST then FSUB), while the box maximum goes through
// the out-of-line Vector3 helpers and rounds it.
Vector3 MorphVector(const float* v, float s, float t, float u, bool keepZ) {
    Vector3 lower = LerpStored(LerpStored(Load(v), Load(v + 3), s), LerpStored(Load(v + 6), Load(v + 9), s), t);
    Vector3 a = LerpStored(Load(v + 12), Load(v + 15), s);
    Vector3 b = LerpStored(Load(v + 18), Load(v + 21), s);
    Vector3 step = Scale(Sub(b, a), t);
    Vector3 diff;
    if (keepZ) {
        diff = {static_cast<float>(static_cast<float>(step.x + a.x) - lower.x), static_cast<float>(static_cast<float>(step.y + a.y) - lower.y), static_cast<float>((static_cast<double>(step.z) + a.z) - lower.z)};
    } else {
        diff = Sub(Add(step, a), lower);
    }
    return Add(Scale(diff, u), lower);
}

// The older 6-corner layout (non-FIG files), as for the bones (figure.h, BoneMorphBox):
//   lerp(lerp(lerp(c0, c1, s), c2, t), lerp(lerp(c3, c4, s), c5, t), u)
// with the same unrounded upper z when `keepZ`.
Vector3 MorphVector6(const float* c, float s, float t, float u, bool keepZ) {
    Vector3 lower = LerpStored(LerpStored(Load(c), Load(c + 3), s), Load(c + 6), t);
    Vector3 a = LerpStored(Load(c + 9), Load(c + 12), s);
    Vector3 step = Scale(Sub(Load(c + 15), a), t);
    Vector3 diff;
    if (keepZ) {
        diff = {static_cast<float>(static_cast<float>(step.x + a.x) - lower.x), static_cast<float>(static_cast<float>(step.y + a.y) - lower.y), static_cast<float>((static_cast<double>(step.z) + a.z) - lower.z)};
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
float LerpP(float a, float b, float s) { return static_cast<float>(static_cast<float>(static_cast<float>(b - a) * s) + a); }

float CornerPerOp(const float* c, float s, float t, float u) {
    return LerpP(LerpP(LerpP(c[0], c[3], s), c[6], t), LerpP(LerpP(c[9], c[12], s), c[15], t), u);
}
float CornerKept(const float* c, float s, float t, float u) {
    float a = static_cast<float>((static_cast<double>(c[3]) - c[0]) * s + c[0]);
    double lower = (static_cast<double>(c[6]) - a) * t + a;
    float b = static_cast<float>((static_cast<double>(c[12]) - c[9]) * s + c[9]);
    double upper = (static_cast<double>(c[15]) - b) * t + b;
    return static_cast<float>((upper - lower) * u + lower);
}
float CornerStoredLower(const float* c, float s, float t, float u) {
    float a = static_cast<float>((static_cast<double>(c[3]) - c[0]) * s + c[0]);
    float lower = static_cast<float>((static_cast<double>(c[6]) - a) * t + a);
    float b = static_cast<float>((static_cast<double>(c[12]) - c[9]) * s + c[9]);
    float step = static_cast<float>((static_cast<double>(c[15]) - b) * t);
    float diff = static_cast<float>((static_cast<double>(b) + step) - lower);
    return static_cast<float>(static_cast<float>(diff * u) + lower);
}
float CornerPerOpKeptSum(const float* c, float s, float t, float u) {
    float lower = LerpP(LerpP(c[0], c[3], s), c[6], t);
    float b = LerpP(c[9], c[12], s);
    float step = static_cast<float>(static_cast<float>(c[15] - b) * t);
    float diff = static_cast<float>((static_cast<double>(b) + step) - lower);
    return static_cast<float>(static_cast<float>(diff * u) + lower);
}
float CornerPerOpKeptUpper(const float* c, float s, float t, float u) {
    float lower = LerpP(LerpP(c[0], c[3], s), c[6], t);
    float b = LerpP(c[9], c[12], s);
    float step = static_cast<float>((static_cast<double>(c[15]) - b) * t);
    float diff = static_cast<float>((static_cast<double>(step) + b) - lower);
    return static_cast<float>(static_cast<float>(diff * u) + lower);
}

// A scalar from 6 corners, kept on the FPU until the result.
float MorphScalar6(const float* r, float s, float t, float u) {
    double a = (static_cast<double>(r[1]) - r[0]) * s + r[0];
    double lower = (r[2] - a) * t + a;
    double b = (static_cast<double>(r[4]) - r[3]) * s + r[3];
    double upper = (r[5] - b) * t + b;
    return static_cast<float>((upper - lower) * u + lower);
}

// A scalar morphed from 8 versions. The inner blends stay on the FPU; 005A18E0 stores the lower
// half (rounded) before the final blend, 005AAE60 keeps it.
float MorphScalar(const float* r, float s, float t, float u, bool storeLower) {
    double a = (static_cast<double>(r[1]) - r[0]) * s + r[0];
    double b = (static_cast<double>(r[3]) - r[2]) * s + r[2];
    double lower = (b - a) * t + a;
    if (storeLower) lower = static_cast<float>(lower);
    double c = (static_cast<double>(r[5]) - r[4]) * s + r[4];
    double d = (static_cast<double>(r[7]) - r[6]) * s + r[6];
    double upper = (d - c) * t + c;
    return static_cast<float>((upper - lower) * u + lower);
}

} // namespace

void CFigureMesh::LoadBounds(char** cursor, float s, float t, float u, std::uint32_t magic) {
    if (magic == kFig8Magic) {
        const float* p = reinterpret_cast<const float*>(*cursor);
        Vector3 c = MorphVector(p, s, t, u, true);
        center_ = {c.x, c.y, c.z};
        *cursor += 0x60;
        p = reinterpret_cast<const float*>(*cursor);
        Vector3 mn = MorphVector(p, s, t, u, true);
        boxMin_ = {mn.x, mn.y, mn.z};
        *cursor += 0x60;
        p = reinterpret_cast<const float*>(*cursor);
        Vector3 mx = MorphVector(p, s, t, u, false);
        boxMax_ = {mx.x, mx.y, mx.z};
        *cursor += 0x60;
        radius_ = MorphScalar(reinterpret_cast<const float*>(*cursor), s, t, u, true);
        *cursor += 0x20;

        std::memcpy(&bounds_.center, &center_, 12);
        bounds_.radius = radius_;
        bounds_.flags |= 1;
        std::memcpy(&bounds_.boxMin, &boxMin_, 12);
        std::memcpy(&bounds_.boxMax, &boxMax_, 12);
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
    std::memcpy(&bounds_.center, &center_, 12);
    bounds_.flags |= 1;
    bounds_.radius = radius_;
    bounds_.SetBox(&boxMin_.x, &boxMax_.x);
}
EI_REPLACE_METHOD(0x005A18E0, CFigureMesh::LoadBounds);

void __stdcall MorphFigureBounds(ClipBounds* out, const float* data, float s, float t, float u, std::uint32_t magic) {
    Vector3 boxMin, boxMax;
    if (magic == kFig8Magic) {
        Vector3 c = MorphVector(data, s, t, u, true);
        Vector3 mn = MorphVector(data + 0x18, s, t, u, true);
        Vector3 mx = MorphVector(data + 0x30, s, t, u, false);
        float radius = MorphScalar(data + 0x48, s, t, u, false);
        out->flags |= 1;
        out->center.x = c.x;
        out->center.y = c.y;
        out->center.z = c.z;
        out->radius = radius;
        boxMin = {mn.x, mn.y, mn.z};
        boxMax = {mx.x, mx.y, mx.z};
    } else {
        std::memcpy(&out->center, data, 12);
        std::memcpy(&boxMin, data + 3, 12);
        std::memcpy(&boxMax, data + 6, 12);
        out->radius = data[9];
        out->flags |= 1;
    }
    out->SetBox(&boxMin.x, &boxMax.x);
}

void __stdcall MorphFigureBounds6(ClipBounds* out, const float* data, float s, float t, float u) {
    Vector3 c = MorphVector6(data, s, t, u, true);
    Vector3 mn = MorphVector6(data + 0x12, s, t, u, false);
    Vector3 mx = MorphVector6(data + 0x24, s, t, u, false);
    float radius = MorphScalar6(data + 0x36, s, t, u);
    out->center.x = c.x;
    out->center.y = c.y;
    out->center.z = c.z;
    out->flags |= 1;
    out->radius = radius;
    Vector3 boxMin{mn.x, mn.y, mn.z};
    Vector3 boxMax{mx.x, mx.y, mx.z};
    out->SetBox(&boxMin.x, &boxMax.x);
}

namespace {

// One 16-byte block of a FIG8 vertex: `v` holds the block's 8 corner versions (8 x 4 floats).
// The x component stays on the FPU through every blend; y, z and w are stored after each one.
__declspec(noinline) void MorphBlock(float* out, const float* v, float s, float t, float u) {
    // Lower half (versions 0-3), written to `out`.
    {
        double ax = (static_cast<double>(v[4]) - v[0]) * s + v[0];
        float ay = static_cast<float>((static_cast<double>(v[5]) - v[1]) * s + v[1]);
        float az = static_cast<float>((static_cast<double>(v[6]) - v[2]) * s + v[2]);
        float aw = static_cast<float>((static_cast<double>(v[7]) - v[3]) * s + v[3]);
        double bx = (static_cast<double>(v[12]) - v[8]) * s + v[8];
        float by = static_cast<float>((static_cast<double>(v[13]) - v[9]) * s + v[9]);
        float bz = static_cast<float>((static_cast<double>(v[14]) - v[10]) * s + v[10]);
        float bw = static_cast<float>((static_cast<double>(v[15]) - v[11]) * s + v[11]);
        out[0] = static_cast<float>((bx - ax) * t + ax);
        out[1] = static_cast<float>((static_cast<double>(by) - ay) * t + ay);
        out[2] = static_cast<float>((static_cast<double>(bz) - az) * t + az);
        out[3] = static_cast<float>((static_cast<double>(bw) - aw) * t + aw);
    }
    // Upper half (versions 4-7), blended into `out` by u.
    {
        double ax = (static_cast<double>(v[20]) - v[16]) * s + v[16];
        float ay = static_cast<float>((static_cast<double>(v[21]) - v[17]) * s + v[17]);
        float az = static_cast<float>((static_cast<double>(v[22]) - v[18]) * s + v[18]);
        float aw = static_cast<float>((static_cast<double>(v[23]) - v[19]) * s + v[19]);
        double bx = (static_cast<double>(v[28]) - v[24]) * s + v[24];
        float by = static_cast<float>((static_cast<double>(v[29]) - v[25]) * s + v[25]);
        float bz = static_cast<float>((static_cast<double>(v[30]) - v[26]) * s + v[26]);
        float bw = static_cast<float>((static_cast<double>(v[31]) - v[27]) * s + v[27]);
        double cx = (bx - ax) * t + ax;
        float cy = static_cast<float>((static_cast<double>(by) - ay) * t + ay);
        float cz = static_cast<float>((static_cast<double>(bz) - az) * t + az);
        float cw = static_cast<float>((static_cast<double>(bw) - aw) * t + aw);
        out[0] = static_cast<float>((cx - out[0]) * u + out[0]);
        out[1] = static_cast<float>((static_cast<double>(cy) - out[1]) * u + out[1]);
        out[2] = static_cast<float>((static_cast<double>(cz) - out[2]) * u + out[2]);
        out[3] = static_cast<float>((static_cast<double>(cw) - out[3]) * u + out[3]);
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
        out[k] = static_cast<float>((upper - lower) * u + lower);
    }
}

using CornerFn = float (*)(const float*, float, float, float);

// The old-format bounding volume (centre, box minimum, box maximum: 6 corners of 3 floats each;
// radius: 6 floats), written straight into `bounds`. The two loaders round the x component and
// the box minimum differently.
void MorphOldBounds(ClipBounds& bounds, const float* p, float s, float t, float u, CornerFn minX, CornerFn minYZ) {
    Vector3 c = {CornerKept(p, s, t, u), CornerPerOp(p + 1, s, t, u), CornerPerOp(p + 2, s, t, u)};
    const float* q = p + 0x12;
    Vector3 mn = {minX(q, s, t, u), minYZ(q + 1, s, t, u), minYZ(q + 2, s, t, u)};
    q = p + 0x24;
    Vector3 mx = {CornerPerOpKeptSum(q, s, t, u), CornerPerOpKeptSum(q + 1, s, t, u), CornerPerOpKeptSum(q + 2, s, t, u)};
    float radius = MorphScalar6(p + 0x36, s, t, u);
    std::memcpy(&bounds.center, &c, 12);
    bounds.flags |= 1;
    bounds.radius = radius;
    std::memcpy(&bounds.boxMin, &mn, 12);
    std::memcpy(&bounds.boxMax, &mx, 12);
    bounds.flags |= 2;
}

} // namespace

namespace {

// The vertices of a FIG mesh: FIG8 files hold 8 versions of each of the three blocks (0x80 bytes
// each), morphed; other versions hold the three blocks once. The fourth block is set to 1.0.
void LoadVertices(lib::AlignedArray<VertexPacket>& vertices, char** cursor, std::uint32_t magic, float s, float t, float u) {
    const std::uint32_t one = 0x3F800000;
    if (magic == kFig8Magic) {
        for (int i = 0; i < vertices.size; ++i) {
            VertexPacket& v = vertices.data[i];
            for (int b = 0; b < 3; ++b) {
                MorphBlock(v.block[b], reinterpret_cast<const float*>(*cursor), s, t, u);
                *cursor += 0x80;
            }
            for (int k = 3; k >= 0; --k) std::memcpy(&v.block[3][k], &one, 4);
        }
        return;
    }
    for (int i = 0; i < vertices.size; ++i) {
        VertexPacket& v = vertices.data[i];
        for (int b = 0; b < 3; ++b) {
            std::memcpy(v.block[b], *cursor, 16);
            *cursor += 0x10;
        }
        for (int k = 3; k >= 0; --k) std::memcpy(&v.block[3][k], &one, 4);
    }
}

// Sizes `array` to `count` and copies `count` elements from the cursor.
template <typename T>
void LoadArray(lib::AlignedArray<T>& array, char** cursor, int count) {
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
    materialValue_ = Read<std::uint32_t>(*cursor);
    meshType_ = Read<std::uint32_t>(*cursor);
    // 005A18E0 through its address (the verify build checks it there).
    ei::Code<void(__fastcall*)(CFigureMesh*, void*, char**, float, float, float, std::uint32_t)>(0x005A18E0)(
        this, nullptr, cursor, s, t, u, header.magic);

    vertices_.SetSize(header.vertexCount);
    LoadVertices(vertices_, cursor, header.magic, s, t, u);
    if (vertices_.size != 0) FitBounds(&bounds_, vertices_.data, vertices_.size);

    normals_.SetSize(header.normalCount);
    colorsA_.SetSize(header.normalCount * 4);
    colorsB_.SetSize(header.normalCount * 4);
    if (header.normalCount != 0) {
        std::memcpy(normals_.data, *cursor, static_cast<std::size_t>(header.normalCount) * sizeof(NormalPacket));
        *cursor += header.normalCount * 0x40;
    }
    LoadArray(texCoords_, cursor, header.texCoordCount);
    LoadArray(indices_, cursor, header.indexCount);
    LoadArray(corners_, cursor, header.cornerCount);
    LoadArray(words_, cursor, header.wordCount);

    BuffersChanged();
    buffers_.vertices = vertices_.data;
    buffers_.vertexCount = vertices_.size;
    buffers_.normals = normals_.data;
    buffers_.normalCount = normals_.size;
    colorsA_.SetSize(normals_.size * 4);   // 005A6E70 with growBy -1
    colorsB_.SetSize(normals_.size * 4);
    buffers_.texCoords = texCoords_.data;
    buffers_.texCoordCount = texCoords_.size;
    buffers_.indices = indices_.data;
    buffers_.indexCount = indices_.size;
    buffers_.corners = corners_.data;
    buffers_.cornerCount = corners_.size;
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
    materialValue_ = Read<std::uint32_t>(*cursor);
    meshType_ = Read<std::uint32_t>(*cursor);
    ei::Code<void(__fastcall*)(CFigureMesh*, void*, char**, float, float, float, std::uint32_t)>(0x005A18E0)(
        this, nullptr, cursor, s, t, u, header.magic);

    vertices_.SetSize(header.vertexCount);
    LoadVertices(vertices_, cursor, header.magic, s, t, u);
    if (vertices_.size != 0) FitBounds(&bounds_, vertices_.data, vertices_.size);

    normals_.SetSize(header.normalCount);
    colorsA_.SetSize(header.normalCount * 4);
    colorsB_.SetSize(header.normalCount * 4);
    if (header.normalCount != 0) {
        std::memcpy(normals_.data, *cursor, static_cast<std::size_t>(header.normalCount) * sizeof(NormalPacket));
        *cursor += header.normalCount * 0x40;
    }
    // The rest stays in the file data.
    buffers_.texCoords = *cursor;
    buffers_.texCoordCount = header.texCoordCount;
    *cursor += header.texCoordCount * 8;
    buffers_.indices = *cursor;
    buffers_.indexCount = header.indexCount;
    *cursor += header.indexCount * 2;
    buffers_.corners = *cursor;
    buffers_.cornerCount = header.cornerCount;
    *cursor += header.cornerCount * 6;
    buffers_.words = *cursor;
    buffers_.wordCount = header.wordCount;
    *cursor += header.wordCount * 4;

    BuffersChanged();
    buffers_.vertices = vertices_.data;
    buffers_.vertexCount = vertices_.size;
    buffers_.normals = normals_.data;
    buffers_.normalCount = normals_.size;
    colorsA_.SetSize(normals_.size * 4);
    colorsB_.SetSize(normals_.size * 4);
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
    mesh->materialValue_ = Read<std::uint32_t>(*cursor);
    mesh->meshType_ = Read<std::uint32_t>(*cursor);
    if (shared)
        MorphOldBounds(mesh->bounds_, reinterpret_cast<const float*>(*cursor), s, t, u, CornerKept, CornerPerOp);
    else
        MorphOldBounds(mesh->bounds_, reinterpret_cast<const float*>(*cursor), s, t, u, CornerStoredLower, CornerPerOpKeptUpper);
    *cursor += 0xF0;

    int count = ReadCount(cursor);
    mesh->vertices_.SetSize(count);
    for (int i = 0; i < count; ++i) {
        VertexPacket& v = mesh->vertices_.data[i];
        for (int b = 0; b < 3; ++b) {
            MorphBlock6(v.block[b], reinterpret_cast<const float*>(*cursor), s, t, u);
            *cursor += 0x60;
        }
        const std::uint32_t one = 0x3F800000;
        for (int k = 3; k >= 0; --k) std::memcpy(&v.block[3][k], &one, 4);
    }
    if (mesh->vertices_.size != 0) FitBounds(&mesh->bounds_, mesh->vertices_.data, mesh->vertices_.size);

    count = ReadCount(cursor);
    mesh->normals_.SetSize(count);
    mesh->colorsA_.SetSize(count * 4);
    mesh->colorsB_.SetSize(count * 4);
    if (count != 0) {
        std::memcpy(mesh->normals_.data, *cursor, static_cast<std::size_t>(count) * sizeof(NormalPacket));
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
    LoadArray(texCoords_, cursor, ReadCount(cursor));
    SkipRecords34(cursor);
    LoadArray(corners_, cursor, ReadCount(cursor));
    LoadArray(words_, cursor, ReadCount(cursor));
    LoadArray(indices_, cursor, ReadCount(cursor));   // 0044FA90 with growBy -1

    BuffersChanged();
    buffers_.vertices = vertices_.data;
    buffers_.vertexCount = vertices_.size;
    buffers_.normals = normals_.data;
    buffers_.normalCount = normals_.size;
    colorsA_.SetSize(normals_.size * 4);
    colorsB_.SetSize(normals_.size * 4);
    buffers_.texCoords = texCoords_.data;
    buffers_.texCoordCount = texCoords_.size;
    buffers_.corners = corners_.data;
    buffers_.cornerCount = corners_.size;
    buffers_.words = words_.data;
    buffers_.wordCount = words_.size;
    buffers_.indexCount = indices_.size;
    buffers_.indices = indices_.data;
    flags_ |= 0x20;
}
EI_REPLACE_METHOD(0x005A3800, CFigureMesh::LoadOld);

void CFigureMesh::LoadOldShared(char** cursor, float s, float t, float u) {
    // Unlike the other three loaders, this one does not test the loaded flag.
    LoadOldCommon(this, cursor, s, t, u, true);
    // The rest stays in the file data.
    buffers_.texCoordCount = ReadCount(cursor);
    buffers_.texCoords = *cursor;
    *cursor += buffers_.texCoordCount * 8;
    SkipRecords34(cursor);
    buffers_.cornerCount = ReadCount(cursor);
    buffers_.corners = *cursor;
    *cursor += buffers_.cornerCount * 6;
    buffers_.wordCount = ReadCount(cursor);
    buffers_.words = *cursor;
    *cursor += buffers_.wordCount * 4;
    buffers_.indexCount = ReadCount(cursor);
    buffers_.indices = *cursor;
    *cursor += buffers_.indexCount * 2;

    BuffersChanged();
    buffers_.vertices = vertices_.data;
    buffers_.vertexCount = vertices_.size;
    buffers_.normals = normals_.data;
    buffers_.normalCount = normals_.size;
    colorsA_.SetSize(normals_.size * 4);
    colorsB_.SetSize(normals_.size * 4);
    flags_ |= 0x20;
}
EI_REPLACE_METHOD(0x005A4D60, CFigureMesh::LoadOldShared);

// ---- World-space box ----------------------------------------------------------------------------

namespace {

// The limit on the squared distance of the position from the origin (the float 1e8 at 0073FAFC).
inline float FarLimit() { return 100000000.0f /*0073FAFC*/; }
} // namespace

namespace {
// Grows [min, max] by one transformed point. The original selects with the FCOMP status bit C0
// as a mask on the float bits, so a NaN on either side takes the point.
void Extend(float& max, float& min, float v) {
    max = max >= v ? max : v;
    min = v >= min ? min : v;
}
} // namespace

void CFigureMesh::ExtendWorldBox(float* maxX, float* maxY, float* maxZ, float* minX, float* minY, float* minZ) const {
    // ((x*x + y*y) + z*z) of the position, on the FPU; NaN passes.
    double distance = LengthSquaredXYZ(localPosition_);
    if (distance > FarLimit()) return;

    Matrix4 m = {};
    WorldMatrix(m.m);

    // The 8 corners of the box (centre + minimum / maximum), maximum x first, times the matrix as
    // a row vector (ZYX, translation last); each transformed coordinate is stored. The corner
    // sums stay on the FPU in the original (the same at the main thread's 24-bit precision).
    const Vector3& c = bounds_.center;
    const Vector3& lo = bounds_.boxMin;
    const Vector3& hi = bounds_.boxMax;
    for (int corner = 0; corner < 8; ++corner) {
        Vector3 p = {((corner & 4) ? lo : hi).x + c.x, ((corner & 2) ? lo : hi).y + c.y, ((corner & 1) ? lo : hi).z + c.z};
        Vector3 w = TransformRow<Sum3::ZYX>(m, p);
        Extend(*maxX, *minX, w.x);
        Extend(*maxY, *minY, w.y);
        Extend(*maxZ, *minZ, w.z);
    }
}

// ---- Mirroring, buffers, vertex offsets ---------------------------------------------------------

namespace {
// Negates one component block of every packet (FLD / FCHS / FSTP in the original).
template <typename Packet>
void NegateBlock(lib::AlignedArray<Packet>& packets, int block) {
    for (int i = 0; i < packets.size; ++i) {
        float* v = reinterpret_cast<float*>(&packets.data[i]) + block * 4;
        for (int k = 0; k < 4; ++k) v[k] = -v[k];
    }
}

// Swaps the first two corners of every triangle (reverses the winding after a mirror).
void ReverseWinding(lib::AlignedArray<std::uint16_t>& indices) {
    std::uint16_t* p = indices.data;
    for (int i = 0; i < indices.size; i += 3, p += 3) {
        std::uint16_t first = p[0];
        p[0] = p[1];
        p[1] = first;
    }
}

// The shared buffer DrawOffset moves the vertices into (a function-less global at 007C2E18).
lib::AlignedArray<VertexPacket>& OffsetVertices() { return ei::GameData<lib::AlignedArray<VertexPacket>>(0x007C2E18); }
} // namespace

void CFigureMesh::MirrorX() {
    NegateBlock(vertices_, 0);
    NegateBlock(normals_, 0);
    ReverseWinding(indices_);
    BuffersChanged();
}
EI_REPLACE_METHOD(0x005A4B30, CFigureMesh::MirrorX);

void CFigureMesh::FitVertexBounds() {
    if (vertices_.size != 0) FitBounds(&bounds_, vertices_.data, vertices_.size);
}

void CFigureMesh::MirrorY() {
    NegateBlock(vertices_, 1);
    NegateBlock(normals_, 1);
    ReverseWinding(indices_);
    BuffersChanged();
}
EI_REPLACE_METHOD(0x005A4BF0, CFigureMesh::MirrorY);

EI_REPLACE_METHOD(0x005A6E70, lib::AlignedArray<std::uint32_t>::SetSize);

void CFigureMesh::RefreshBuffers() {
    BuffersChanged();
    buffers_.vertices = vertices_.data;
    buffers_.vertexCount = vertices_.size;
    buffers_.normals = normals_.data;
    buffers_.normalCount = normals_.size;
    // 005A6E70 through its address (the verify build checks it there).
    using ResizeFn = void(__fastcall*)(lib::AlignedArray<std::uint32_t>*, void*, int, int);
    ei::Code<ResizeFn>(0x005A6E70)(&colorsA_, nullptr, normals_.size * 4, -1);
    ei::Code<ResizeFn>(0x005A6E70)(&colorsB_, nullptr, normals_.size * 4, -1);
    buffers_.texCoords = texCoords_.data;
    buffers_.texCoordCount = texCoords_.size;
    buffers_.corners = corners_.data;
    buffers_.cornerCount = corners_.size;
    buffers_.words = words_.data;
    buffers_.wordCount = words_.size;
    buffers_.indices = indices_.data;
    buffers_.indexCount = indices_.size;
}

void CFigureMesh::SetOffsets(const OffsetSpan* offsets) {
    // The positions the new renderer keeps on the GPU move with new offsets (R16d): the animation's frame.
    const bool same = offsets_.size == offsets->count &&
                      (offsets->count == 0 ||
                       std::memcmp(offsets_.data, offsets->data, static_cast<std::size_t>(offsets->count) * sizeof(Vector3)) == 0);
    offsets_.SetSize(offsets->count);
    std::memcpy(offsets_.data, offsets->data, static_cast<std::size_t>(offsets->count) * sizeof(Vector3));
    if (!same) render::MeshChanged(this, render::MeshChange::Positions);
}

void CFigureMesh::BuffersChanged() { render::MeshChanged(this, render::MeshChange::Rebuilt); }

int CFigureMesh::DrawOffset(CTransformPipeline* pipeline, const float* matrix, Vector2* points, float* bounds) {
    void* vertices = buffers_.vertices;
    if (offsets_.size != 0) {
        const VertexPacket* source = static_cast<const VertexPacket*>(buffers_.vertices);
        lib::AlignedArray<VertexPacket>& moved = OffsetVertices();
        // Sized like SetSize, except that a larger buffer is not given the old contents (they are
        // all rewritten below) and 0 allocates an empty buffer instead of freeing it.
        int packets = buffers_.vertexCount;
        if (!moved.data) {
            moved.data = lib::AlignedArray<VertexPacket>::Allocate(packets);
            moved.capacity = packets;
            moved.size = packets;
        } else if (packets <= moved.capacity) {
            moved.size = packets;
        } else {
            int step = moved.growBy;
            if (step == 0) {
                step = moved.size / 8;
                if (step < 4) step = 4;
                else if (step > 0x400) step = 0x400;
            }
            int newCapacity = moved.capacity + step;
            if (packets >= newCapacity) newCapacity = packets;
            VertexPacket* fresh = lib::AlignedArray<VertexPacket>::Allocate(newCapacity);
            lib::AlignedArray<VertexPacket>::Free(moved.data);
            moved.data = fresh;
            moved.size = packets;
            moved.capacity = newCapacity;
        }
        // Vertex i is component i & 3 of packet i >> 2; each coordinate is offset + source.
        for (int i = 0; i < moved.size * 4; ++i) {
            const float* from = &source[i >> 2].block[0][i & 3];
            float* to = &moved.data[i >> 2].block[0][i & 3];
            if (i < offsets_.size) {
                const Vector3& d = offsets_.data[i];
                to[0] = d.x + from[0];
                to[4] = d.y + from[4];
                to[8] = d.z + from[8];
            } else {
                // Copied as dwords.
                std::memcpy(&to[0], &from[0], 4);
                std::memcpy(&to[4], &from[4], 4);
                std::memcpy(&to[8], &from[8], 4);
            }
        }
        vertices = moved.data;
    }
    pipeline->transform_->ProjectFlat(matrix, static_cast<const VertexPacket*>(vertices), points, buffers_.vertexCount,
                                      bounds);
    return buffers_.vertexCount << 2;
}
