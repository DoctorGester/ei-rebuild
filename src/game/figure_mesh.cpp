// Figure mesh loaders: the FIG mesh data of a figure part, and the figure loader's bounding volume morphed by the
// figure's three morph coefficients. See docs/notes/world.md ("Figure meshes").
#include "figure_mesh.h"

#include "core_maths.h"
#include "core_transform.h"
#include "link_figure.h"
#include "model.h"
#include "render/render.h"

#include "lib/bytes.h"

#include "ei/x87.h"

#include <cstring>
#include <vector>

namespace {

template <typename T>
T Read(char*& p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return v;
}

// `count` elements of the file data at the cursor, kept in place (the mesh points into them); the cursor moves past them.
template <typename T>
T* InFile(char** cursor, int count) {
    T* at = reinterpret_cast<T*>(*cursor);
    *cursor += count * static_cast<int>(sizeof(T));
    return at;
}

// Inline Vector3 arithmetic of the original: every component of every operation is rounded to a
// float (each result goes through a Vector3 temporary).
Vector3 Load(const float* p) { return {p[0], p[1], p[2]}; }
Vector3 Sub(const Vector3& a, const Vector3& b) { return {static_cast<float>(a.x - b.x), static_cast<float>(a.y - b.y), static_cast<float>(a.z - b.z)}; }
Vector3 Add(const Vector3& a, const Vector3& b) { return {static_cast<float>(a.x + b.x), static_cast<float>(a.y + b.y), static_cast<float>(a.z + b.z)}; }
Vector3 Scale(const Vector3& a, float s) { return {static_cast<float>(a.x * s), static_cast<float>(a.y * s), static_cast<float>(a.z * s)}; }
// a + (b - a) * s, written (b - a) * s + a as the original evaluates it.
Vector3 LerpStored(const Vector3& a, const Vector3& b, float s) { return Add(Scale(Sub(b, a), s), a); }

// One morphed vector from 8 corner versions (`v` points at 8 Vector3s):
// lerp(lerp(lerp(v0, v1, s), lerp(v2, v3, s), t), lerp(lerp(v4, v5, s), lerp(v6, v7, s), t), u)
// `keepZ`: the original keeps the z component of the upper blend unrounded on the FPU for the
// final difference (centre and box minimum: stored as a float, then subtracted), while the box maximum goes through
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

// The older 6-corner layout (non-FIG files), as for the bones (six corners per bone):
// lerp(lerp(lerp(c0, c1, s), c2, t), lerp(lerp(c3, c4, s), c5, t), u)
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

// A scalar from 6 corners, kept on the FPU until the result.
float MorphScalar6(const float* r, float s, float t, float u) {
    double a = (static_cast<double>(r[1]) - r[0]) * s + r[0];
    double lower = (r[2] - a) * t + a;
    double b = (static_cast<double>(r[4]) - r[3]) * s + r[3];
    double upper = (r[5] - b) * t + b;
    return static_cast<float>((upper - lower) * u + lower);
}

// A scalar morphed from 8 versions, every blend on the FPU.
float MorphScalar(const float* r, float s, float t, float u) {
    double a = (static_cast<double>(r[1]) - r[0]) * s + r[0];
    double b = (static_cast<double>(r[3]) - r[2]) * s + r[2];
    double lower = (b - a) * t + a;
    double c = (static_cast<double>(r[5]) - r[4]) * s + r[4];
    double d = (static_cast<double>(r[7]) - r[6]) * s + r[6];
    double upper = (d - c) * t + c;
    return static_cast<float>((upper - lower) * u + lower);
}

// FIG8 files hold 8 versions of every bound and vertex block, the old format 6 (the corners of the figure's
// morph). The meshes loaded here are never morphed (all weights 0), so they take the first version.
constexpr int kFig8Versions = 8;
constexpr int kOldVersions = 6;

// Reads the first of `versions` copies of a value at the cursor (a byte copy, floats too) and moves past all of them.
template <typename T>
void ReadFirstVersion(T& value, char** cursor, int versions) {
    std::memcpy(&value, *cursor, sizeof(T));
    *cursor += static_cast<int>(sizeof(T)) * versions;
}

// The vertices of a mesh: three 16-byte blocks each, `versions` copies of every block. The fourth block is set
// to 1.0.
void LoadVertices(std::vector<VertexPacket>& vertices, char** cursor, int versions) {
    for (VertexPacket& v : vertices) {
        for (int b = 0; b < 3; ++b) ReadFirstVersion(v.block[b], cursor, versions);
        for (int k = 3; k >= 0; --k) v.block[3][k] = 1.0f;
    }
}

template <typename T>
void LoadArray(std::vector<T>& array, char** cursor, int count) {
    array.resize(static_cast<std::size_t>(count));
    if (count != 0) {
        std::memcpy(array.data(), *cursor, static_cast<std::size_t>(count) * sizeof(T));
        *cursor += count * static_cast<int>(sizeof(T));
    }
}

int ReadCount(char** cursor) {
    int n;
    lib::LoadBytes(n, *cursor);
    *cursor += 4;
    return n;
}

// The part both old-format loaders share: header words, bounds, vertices and records (the records' second colour
// array is set to opaque black).
void LoadOldCommon(FigureMesh* mesh, char** cursor) {
    mesh->materialValue = Read<std::uint32_t>(*cursor);
    mesh->meshType = Read<std::uint32_t>(*cursor);
    ClipBounds& bounds = mesh->box.bounds;
    ReadFirstVersion(bounds.center, cursor, kOldVersions);
    ReadFirstVersion(bounds.boxMin, cursor, kOldVersions);
    ReadFirstVersion(bounds.boxMax, cursor, kOldVersions);
    ReadFirstVersion(bounds.radius, cursor, kOldVersions);
    bounds.flags |= BoundsFlag::Sphere;
    bounds.flags |= BoundsFlag::Box;

    int count = ReadCount(cursor);
    mesh->vertices.resize(count);
    LoadVertices(mesh->vertices, cursor, kOldVersions);
    if (!mesh->vertices.empty()) bounds.Fit(mesh->vertices.data(), std::ssize(mesh->vertices));

    count = ReadCount(cursor);
    mesh->normals.resize(count);
    mesh->colorsA.resize(count * 4);
    mesh->colorsB.resize(count * 4);
    if (count != 0) {
        std::memcpy(mesh->normals.data(), *cursor, static_cast<std::size_t>(count) * sizeof(NormalPacket));
        *cursor += count * 0x40;
        for (int i = 0; i < std::ssize(mesh->colorsB); ++i) mesh->colorsB[i] = 0xFF000000;
    }
}

// A block of 0x34-byte records the loaders skip.
void SkipRecords34(char** cursor) {
    int n = ReadCount(cursor);
    *cursor += n * 0x34;
}

// The limit on the squared distance of the position from the origin (the float 1e8).
inline constexpr float kFarLimit = 100000000.0f;

// Grows [min, max] by one transformed point. The original selects with the comparison's below-or-unordered bit
// as a mask on the float bits, so a NaN on either side takes the point.
void Extend(float& max, float& min, float v) {
    max = max >= v ? max : v;
    min = v >= min ? min : v;
}

// Negates one component block of every packet.
template <typename Packet>
void NegateBlock(std::vector<Packet>& packets, int block) {
    for (int i = 0; i < std::ssize(packets); ++i) {
        float* v = packets[i].block[block];
        for (int k = 0; k < 4; ++k) v[k] = -v[k];
    }
}

// Swaps the first two corners of every triangle (reverses the winding after a mirror).
void ReverseWinding(std::vector<std::uint16_t>& indices) {
    std::uint16_t* p = indices.data();
    for (int i = 0; i < std::ssize(indices); i += 3, p += 3) {
        std::uint16_t first = p[0];
        p[0] = p[1];
        p[1] = first;
    }
}

} // namespace

// Each value the original stores to a float is rounded with static_cast<float>; everything else
// stays on the FPU.

void FigureMesh::LoadBounds(char** cursor, std::uint32_t magic) {
    const int versions = magic == kFig8Magic ? kFig8Versions : 1;
    ReadFirstVersion(center, cursor, versions);
    ReadFirstVersion(boxMin, cursor, versions);
    ReadFirstVersion(boxMax, cursor, versions);
    ReadFirstVersion(radius, cursor, versions);
    box.bounds.center = center;
    box.bounds.flags |= BoundsFlag::Sphere;
    box.bounds.radius = radius;
    box.bounds.SetBox(&boxMin.x, &boxMax.x);
}

void MorphFigureBounds(ClipBounds* out, const float* data, float s, float t, float u, std::uint32_t magic) {
    Vector3 boxMin, boxMax;
    if (magic == kFig8Magic) {
        Vector3 c = MorphVector(data, s, t, u, true);
        Vector3 mn = MorphVector(data + 0x18, s, t, u, true);
        Vector3 mx = MorphVector(data + 0x30, s, t, u, false);
        float radius = MorphScalar(data + 0x48, s, t, u);
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

void MorphFigureBounds6(ClipBounds* out, const float* data, float s, float t, float u) {
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

void PlainFigureMesh::LoadFig(char** cursor) {
    if (flags & FigureMeshFlag::Loaded) return;
    FigHeader header;
    lib::LoadBytes(header, *cursor);
    *cursor += sizeof(header);
    materialValue = Read<std::uint32_t>(*cursor);
    meshType = Read<std::uint32_t>(*cursor);
    LoadBounds(cursor, header.magic);

    vertices.resize(header.vertexCount);
    LoadVertices(vertices, cursor, header.magic == kFig8Magic ? kFig8Versions : 1);
    if (!vertices.empty()) box.bounds.Fit(vertices.data(), std::ssize(vertices));

    normals.resize(header.normalCount);
    colorsA.resize(header.normalCount * 4);
    colorsB.resize(header.normalCount * 4);
    if (header.normalCount != 0) {
        std::memcpy(normals.data(), *cursor, static_cast<std::size_t>(header.normalCount) * sizeof(NormalPacket));
        *cursor += header.normalCount * 0x40;
    }
    LoadArray(texCoords, cursor, header.texCoordCount);
    LoadArray(indices, cursor, header.indexCount);
    LoadArray(corners, cursor, header.cornerCount);
    LoadArray(words, cursor, header.wordCount);

    BuffersChanged();
    buffers.vertices = vertices.data();
    buffers.vertexCount = std::ssize(vertices);
    buffers.normals = normals.data();
    buffers.normalCount = std::ssize(normals);
    colorsA.resize(std::ssize(normals) * 4);
    colorsB.resize(std::ssize(normals) * 4);
    buffers.texCoords = texCoords.data();
    buffers.texCoordCount = std::ssize(texCoords);
    buffers.indices = indices.data();
    buffers.indexCount = std::ssize(indices);
    buffers.corners = corners.data();
    buffers.cornerCount = std::ssize(corners);
    buffers.wordCount = std::ssize(words);
    buffers.words = words.data();
    flags |= FigureMeshFlag::Loaded;
}

void SharedFigureMesh::LoadFig(char** cursor) {
    if (flags & FigureMeshFlag::Loaded) return;
    FigHeader header;
    lib::LoadBytes(header, *cursor);
    *cursor += sizeof(header);
    materialValue = Read<std::uint32_t>(*cursor);
    meshType = Read<std::uint32_t>(*cursor);
    LoadBounds(cursor, header.magic);

    vertices.resize(header.vertexCount);
    LoadVertices(vertices, cursor, header.magic == kFig8Magic ? kFig8Versions : 1);
    if (!vertices.empty()) box.bounds.Fit(vertices.data(), std::ssize(vertices));

    normals.resize(header.normalCount);
    colorsA.resize(header.normalCount * 4);
    colorsB.resize(header.normalCount * 4);
    if (header.normalCount != 0) {
        std::memcpy(normals.data(), *cursor, static_cast<std::size_t>(header.normalCount) * sizeof(NormalPacket));
        *cursor += header.normalCount * 0x40;
    }
    // The rest stays in the file data.
    buffers.texCoords = InFile<TexCoord>(cursor, header.texCoordCount);
    buffers.texCoordCount = header.texCoordCount;
    buffers.indices = InFile<std::uint16_t>(cursor, header.indexCount);
    buffers.indexCount = header.indexCount;
    buffers.corners = InFile<MeshCorner>(cursor, header.cornerCount);
    buffers.cornerCount = header.cornerCount;
    buffers.words = InFile<std::uint32_t>(cursor, header.wordCount);
    buffers.wordCount = header.wordCount;

    BuffersChanged();
    buffers.vertices = vertices.data();
    buffers.vertexCount = std::ssize(vertices);
    buffers.normals = normals.data();
    buffers.normalCount = std::ssize(normals);
    colorsA.resize(std::ssize(normals) * 4);
    colorsB.resize(std::ssize(normals) * 4);
    flags |= FigureMeshFlag::Loaded;
}

// ---- Old (non-FIG) format -----------------------------------------------------------------------

void PlainFigureMesh::LoadOld(char** cursor) {
    if (flags & FigureMeshFlag::Loaded) return;
    LoadOldCommon(this, cursor);
    LoadArray(texCoords, cursor, ReadCount(cursor));
    SkipRecords34(cursor);
    LoadArray(corners, cursor, ReadCount(cursor));
    LoadArray(words, cursor, ReadCount(cursor));
    LoadArray(indices, cursor, ReadCount(cursor));

    BuffersChanged();
    buffers.vertices = vertices.data();
    buffers.vertexCount = std::ssize(vertices);
    buffers.normals = normals.data();
    buffers.normalCount = std::ssize(normals);
    colorsA.resize(std::ssize(normals) * 4);
    colorsB.resize(std::ssize(normals) * 4);
    buffers.texCoords = texCoords.data();
    buffers.texCoordCount = std::ssize(texCoords);
    buffers.corners = corners.data();
    buffers.cornerCount = std::ssize(corners);
    buffers.words = words.data();
    buffers.wordCount = std::ssize(words);
    buffers.indexCount = std::ssize(indices);
    buffers.indices = indices.data();
    flags |= FigureMeshFlag::Loaded;
}

void SharedFigureMesh::LoadOld(char** cursor) {
    // Unlike the other three loaders, this one does not test the loaded flag.
    LoadOldCommon(this, cursor);
    // The rest stays in the file data.
    buffers.texCoordCount = ReadCount(cursor);
    buffers.texCoords = InFile<TexCoord>(cursor, buffers.texCoordCount);
    SkipRecords34(cursor);
    buffers.cornerCount = ReadCount(cursor);
    buffers.corners = InFile<MeshCorner>(cursor, buffers.cornerCount);
    buffers.wordCount = ReadCount(cursor);
    buffers.words = InFile<std::uint32_t>(cursor, buffers.wordCount);
    buffers.indexCount = ReadCount(cursor);
    buffers.indices = InFile<std::uint16_t>(cursor, buffers.indexCount);

    BuffersChanged();
    buffers.vertices = vertices.data();
    buffers.vertexCount = std::ssize(vertices);
    buffers.normals = normals.data();
    buffers.normalCount = std::ssize(normals);
    colorsA.resize(std::ssize(normals) * 4);
    colorsB.resize(std::ssize(normals) * 4);
    flags |= FigureMeshFlag::Loaded;
}

// ---- World-space box ----------------------------------------------------------------------------

void FigureMesh::ExtendWorldBox(float* maxX, float* maxY, float* maxZ, float* minX, float* minY, float* minZ) const {
    // ((x*x + y*y) + z*z) of the position, on the FPU; NaN passes.
    double distance = LengthSquaredXYZ(localPosition);
    if (distance > kFarLimit) return;

    Matrix4 m = {};
    WorldMatrix(m.m);

    // The 8 corners of the box (centre + minimum / maximum), maximum x first, times the matrix as
    // a row vector (ZYX, translation last); each transformed coordinate is stored. The corner
    // sums stay on the FPU in the original (the same at the main thread's 24-bit precision).
    const Vector3& c = box.bounds.center;
    const Vector3& lo = box.bounds.boxMin;
    const Vector3& hi = box.bounds.boxMax;
    for (int corner = 0; corner < 8; ++corner) {
        Vector3 p = {((corner & 4) ? lo : hi).x + c.x, ((corner & 2) ? lo : hi).y + c.y, ((corner & 1) ? lo : hi).z + c.z};
        Vector3 w = TransformRow<Sum3::ZYX>(m, p);
        Extend(*maxX, *minX, w.x);
        Extend(*maxY, *minY, w.y);
        Extend(*maxZ, *minZ, w.z);
    }
}

// ---- Mirroring, buffers, vertex offsets ---------------------------------------------------------

void PlainFigureMesh::MirrorX() {
    NegateBlock(vertices, 0);
    NegateBlock(normals, 0);
    ReverseWinding(indices);
    BuffersChanged();
}

void FigureMesh::FitVertexBounds() {
    if (!vertices.empty()) box.bounds.Fit(vertices.data(), std::ssize(vertices));
}

void PlainFigureMesh::MirrorY() {
    NegateBlock(vertices, 1);
    NegateBlock(normals, 1);
    ReverseWinding(indices);
    BuffersChanged();
}

void PlainFigureMesh::RefreshBuffers() {
    BuffersChanged();
    buffers.vertices = vertices.data();
    buffers.vertexCount = std::ssize(vertices);
    buffers.normals = normals.data();
    buffers.normalCount = std::ssize(normals);
    colorsA.resize(std::ssize(normals) * 4);
    colorsB.resize(std::ssize(normals) * 4);
    buffers.texCoords = texCoords.data();
    buffers.texCoordCount = std::ssize(texCoords);
    buffers.corners = corners.data();
    buffers.cornerCount = std::ssize(corners);
    buffers.words = words.data();
    buffers.wordCount = std::ssize(words);
    buffers.indices = indices.data();
    buffers.indexCount = std::ssize(indices);
}

void FigureMesh::SetOffsets(const OffsetSpan* newOffsets) {
    // The positions the new renderer keeps on the GPU move with new offsets: the animation's frame.
    const bool same = std::ssize(offsets) == newOffsets->count &&
                      (newOffsets->count == 0 ||
                       std::memcmp(offsets.data(), newOffsets->data, static_cast<std::size_t>(newOffsets->count) * sizeof(Vector3)) == 0);
    offsets.resize(newOffsets->count);
    std::memcpy(offsets.data(), newOffsets->data, static_cast<std::size_t>(newOffsets->count) * sizeof(Vector3));
    if (!same) render::MeshChanged(this, render::MeshChange::Positions);
}

void FigureMesh::BuffersChanged() { render::MeshChanged(this, render::MeshChange::Rebuilt); }
