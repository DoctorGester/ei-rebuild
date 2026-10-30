// Figure mesh loaders: the FIG mesh data of a figure part, and the figure loader's bounding volume morphed by the
// figure's three morph coefficients. See docs/notes/world.md ("Figure meshes").
#include "figure_mesh.h"

#include "core_maths.h"
#include "core_transform.h"
#include "figure_manager.h"
#include "item.h"
#include "link_figure.h"
#include "manager_context.h"
#include "model.h"
#include "object_maths.h"
#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/float_select.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "platform/file.h"
#include "render/render.h"

#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace {
constexpr std::uint32_t kFigMagic = 0x474946;      // "FIG"

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
Vector3 LerpStored(const Vector3& from, const Vector3& to, float s) { return Add(Scale(Sub(to, from), s), from); }

// One morphed vector from 8 corner versions (`v` points at 8 Vector3s):
// lerp(lerp(lerp(v0, v1, s), lerp(v2, v3, s), t), lerp(lerp(v4, v5, s), lerp(v6, v7, s), t), u).
// The original keeps the upper blend's z on the FPU for the centre and the box minimum: at the main thread's
// 24-bit precision that is the stored float.
Vector3 MorphVector(const float* v, float s, float t, float u) {
    Vector3 lower = LerpStored(LerpStored(Load(v), Load(v + 3), s), LerpStored(Load(v + 6), Load(v + 9), s), t);
    Vector3 a = LerpStored(Load(v + 12), Load(v + 15), s);
    Vector3 b = LerpStored(Load(v + 18), Load(v + 21), s);
    Vector3 step = Scale(Sub(b, a), t);
    return Add(Scale(Sub(Add(step, a), lower), u), lower);
}

// The older 6-corner layout (non-FIG files), as for the bones (six corners per bone):
// lerp(lerp(lerp(c0, c1, s), c2, t), lerp(lerp(c3, c4, s), c5, t), u).
Vector3 MorphVector6(const float* c, float s, float t, float u) {
    Vector3 lower = LerpStored(LerpStored(Load(c), Load(c + 3), s), Load(c + 6), t);
    Vector3 a = LerpStored(Load(c + 9), Load(c + 12), s);
    Vector3 step = Scale(Sub(Load(c + 15), a), t);
    return Add(Scale(Sub(Add(step, a), lower), u), lower);
}

// A scalar from 6 corners, kept on the FPU until the result.
float MorphScalar6(const float* r, float s, float t, float u) {
    float a = (r[1] - r[0]) * s + r[0];
    float lower = (r[2] - a) * t + a;
    float b = (r[4] - r[3]) * s + r[3];
    float upper = (r[5] - b) * t + b;
    return (upper - lower) * u + lower;
}

// A scalar morphed from 8 versions, every blend on the FPU.
float MorphScalar(const float* r, float s, float t, float u) {
    float a = (r[1] - r[0]) * s + r[0];
    float b = (r[3] - r[2]) * s + r[2];
    float lower = (b - a) * t + a;
    float c = (r[5] - r[4]) * s + r[4];
    float d = (r[7] - r[6]) * s + r[6];
    float upper = (d - c) * t + c;
    return (upper - lower) * u + lower;
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
    if (!mesh->vertices.empty()) bounds.Fit(mesh->vertices.data(), lib::Count(mesh->vertices));

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
    max = MaxKeepB(max, v);
    min = MinKeepA(v, min);
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
        Vector3 c = MorphVector(data, s, t, u);
        Vector3 mn = MorphVector(data + 0x18, s, t, u);
        Vector3 mx = MorphVector(data + 0x30, s, t, u);
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
    Vector3 c = MorphVector6(data, s, t, u);
    Vector3 mn = MorphVector6(data + 0x12, s, t, u);
    Vector3 mx = MorphVector6(data + 0x24, s, t, u);
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
    if (!vertices.empty()) box.bounds.Fit(vertices.data(), lib::Count(vertices));

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
    buffers.vertexCount = lib::Count(vertices);
    buffers.normals = normals.data();
    buffers.normalCount = lib::Count(normals);
    colorsA.resize(std::ssize(normals) * 4);
    colorsB.resize(std::ssize(normals) * 4);
    buffers.texCoords = texCoords.data();
    buffers.texCoordCount = lib::Count(texCoords);
    buffers.indices = indices.data();
    buffers.indexCount = lib::Count(indices);
    buffers.corners = corners.data();
    buffers.cornerCount = lib::Count(corners);
    buffers.wordCount = lib::Count(words);
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
    if (!vertices.empty()) box.bounds.Fit(vertices.data(), lib::Count(vertices));

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
    buffers.vertexCount = lib::Count(vertices);
    buffers.normals = normals.data();
    buffers.normalCount = lib::Count(normals);
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
    buffers.vertexCount = lib::Count(vertices);
    buffers.normals = normals.data();
    buffers.normalCount = lib::Count(normals);
    colorsA.resize(std::ssize(normals) * 4);
    colorsB.resize(std::ssize(normals) * 4);
    buffers.texCoords = texCoords.data();
    buffers.texCoordCount = lib::Count(texCoords);
    buffers.corners = corners.data();
    buffers.cornerCount = lib::Count(corners);
    buffers.words = words.data();
    buffers.wordCount = lib::Count(words);
    buffers.indexCount = lib::Count(indices);
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
    buffers.vertexCount = lib::Count(vertices);
    buffers.normals = normals.data();
    buffers.normalCount = lib::Count(normals);
    colorsA.resize(std::ssize(normals) * 4);
    colorsB.resize(std::ssize(normals) * 4);
    flags |= FigureMeshFlag::Loaded;
}

// ---- World-space box ----------------------------------------------------------------------------

void FigureMesh::ExtendWorldBox(float* maxX, float* maxY, float* maxZ, float* minX, float* minY, float* minZ) const {
    // ((x*x + y*y) + z*z) of the position, on the FPU; NaN passes.
    double distance = LengthSquaredXYZ(localPosition);
    if (distance > kFarLimit) return;

    Matrix4 m = WorldMatrix();

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
    if (!vertices.empty()) box.bounds.Fit(vertices.data(), lib::Count(vertices));
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
    buffers.vertexCount = lib::Count(vertices);
    buffers.normals = normals.data();
    buffers.normalCount = lib::Count(normals);
    colorsA.resize(std::ssize(normals) * 4);
    colorsB.resize(std::ssize(normals) * 4);
    buffers.texCoords = texCoords.data();
    buffers.texCoordCount = lib::Count(texCoords);
    buffers.corners = corners.data();
    buffers.cornerCount = lib::Count(corners);
    buffers.words = words.data();
    buffers.wordCount = lib::Count(words);
    buffers.indices = indices.data();
    buffers.indexCount = lib::Count(indices);
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

void FigureMesh::LoadFromMemory(char** cursor) {
    if (flags & FigureMeshFlag::Loaded) return;
    std::uint32_t magic;
    lib::LoadBytes(magic, *cursor);
    if ((magic & 0xFFFFFF) == kFigMagic) LoadFig(cursor);
    else LoadOld(cursor);
    flags |= FigureMeshFlag::Loaded;
}

// --- File --------------------------------------------------------------------------------------------

// FigureMesh's own file format (reached through virtuals only): the mesh is written to a file and read
// back. The name, then every array as a count and its elements, then the bounding values. See figure_mesh.h
// and docs/notes/world.md.

namespace {

void Write(platform::File* file, const void* data, int size) { file->Write(data, static_cast<unsigned>(size)); }
void Read(platform::File* file, void* data, int size) { file->Read(data, static_cast<unsigned>(size)); }

void WriteCount(platform::File* file, int count) { Write(file, &count, 4); }
int ReadCount(platform::File* file) {
    int count;
    Read(file, &count, 4);
    return count;
}

} // namespace

void FigureMesh::SaveTransform(platform::File* file) {
    int length = static_cast<int>(name.size());
    WriteCount(file, length);
    Write(file, name.c_str(), length);
    WriteCount(file, buffers.vertexCount);
    Write(file, buffers.vertices, buffers.vertexCount * 0x40);
    WriteCount(file, buffers.normalCount);
    Write(file, buffers.normals, buffers.normalCount * 0x40);
    WriteCount(file, buffers.texCoordCount);
    Write(file, buffers.texCoords, buffers.texCoordCount * 8);
    WriteCount(file, 0);
    WriteCount(file, buffers.cornerCount);
    Write(file, buffers.corners, buffers.cornerCount * 6);
    WriteCount(file, buffers.wordCount);
    Write(file, buffers.words, buffers.wordCount * 4);
    WriteCount(file, buffers.indexCount);
    Write(file, buffers.indices, buffers.indexCount * 2);
    Write(file, &center, 12);
    Write(file, &boxMin, 12);
    Write(file, &boxMax, 12);
    Write(file, &radius, 4);
    Write(file, &materialValue, 4);
    Write(file, &meshType, 4);
}

void PlainFigureMesh::LoadTransform(platform::File* file) {
    BuffersChanged();
    int length = ReadCount(file);
    Read(file, str::SetLength(name, length), length);
    // The arrays the file fills (the renderer's descriptor is not updated here).
    int count = ReadCount(file);
    vertices.resize(count);
    Read(file, vertices.data(), count * 0x40);
    count = ReadCount(file);
    normals.resize(count);
    Read(file, normals.data(), count * 0x40);
    count = ReadCount(file);
    texCoords.resize(count);
    Read(file, texCoords.data(), count * 8);
    ReadCount(file);   // the 0 the writer puts here
    count = ReadCount(file);
    corners.resize(count);
    Read(file, corners.data(), count * 6);
    count = ReadCount(file);
    words.resize(count);
    Read(file, words.data(), count * 4);
    count = ReadCount(file);
    indices.resize(count);
    Read(file, indices.data(), count * 2);
    Read(file, &center, 12);
    Read(file, &boxMin, 12);
    Read(file, &boxMax, 12);
    Read(file, &radius, 4);
    Read(file, &materialValue, 4);
    Read(file, &meshType, 4);
}

// ---- Copy and pose --------------------------------------------------------------------------------

void FigureMesh::CopyFigure(const FigureMesh* other) {
    BuffersChanged();
    TransformObject::CopyTransform(*other);   // the transform base's copy
    name = other->name;
    buffers.corners = other->buffers.corners;
    buffers.cornerCount = other->buffers.cornerCount;
    buffers.words = other->buffers.words;
    buffers.wordCount = other->buffers.wordCount;
    buffers.indices = other->buffers.indices;
    buffers.indexCount = other->buffers.indexCount;
    buffers.texCoords = other->buffers.texCoords;
    buffers.texCoordCount = other->buffers.texCoordCount;
    textureOffsetU = 0;
    textureOffsetV = 0;
    center = other->center;
    boxMin = other->boxMin;
    boxMax = other->boxMax;
    radius = other->radius;
    materialValue = other->materialValue;
    meshType = other->meshType;
}

void FigureMesh::UpdateWorld(const Quaternion& rotation, const Vector3& offset) {
    // The pose virtual stores the quaternion as the world rotation.
    CombineRotation(rotation);
    // The position (+0x1C) rotated by the quaternion (w, v):
    // 2 (v.p) v + 2w (v x p) + (w^2 - v.v) p, then moved by `offset`, into +0x28.
    Vector3 v{rotation.x, rotation.y, rotation.z};
    auto* p = &localPosition;
    float dot = DotUnrounded<Sum3::ZYX>(v, *p);
    float twice = dot + dot;
    float z = v.z * twice;
    float y = v.y * twice;
    float x = twice * v.x;
    Vector3 along;
    along.Construct(x, y, z);
    float w = rotation.w;
    Vector3 turned = ScaleVector(Cross(v, *p), w + w);
    float ww = w * w;
    float k = ww - Dot(v, v);
    Vector3 rotated = AddVectors(AddVectors(ScaleVector(*p, k), turned), along);
    float* moved = &worldPosition.x;
    float mx = rotated.x + offset.x;
    float my = rotated.y + offset.y;
    float mz = rotated.z + offset.z;
    moved[0] = mx;
    moved[1] = my;
    moved[2] = mz;

    // The rotation matrix (row-major 4x4) from the stored quaternion (w, x, y, z).
    float* m = poseMatrix.m;
    std::memset(m, 0, sizeof(poseMatrix));
    m[15] = 1.0f;
    m[10] = 1.0f;
    m[5] = 1.0f;
    m[0] = 1.0f;
    float qw = worldRotation.w, qx = worldRotation.x, qy = worldRotation.y, qz = worldRotation.z;
    float x2 = qx + qx;
    float y2 = qy + qy;
    float z2 = qz + qz;
    float wx = x2 * qw;
    float wy = y2 * qw;
    float wz = z2 * qw;
    float xx = x2 * qx;
    float xy = y2 * qx;
    float xz = z2 * qx;
    float yy = y2 * qy;
    float yz = z2 * qy;
    float zz = z2 * qz;
    m[0] = 1.0f - (zz + yy);
    m[4] = xy - wz;
    m[8] = wy + xz;
    m[1] = xy + wz;
    m[5] = 1.0f - (zz + xx);
    m[9] = yz - wx;
    m[2] = xz - wy;
    m[6] = yz + wx;
    m[10] = 1.0f - (yy + xx);
    m[12] = moved[0];
    m[13] = moved[1];
    m[14] = moved[2];
}

// --- Life --------------------------------------------------------------------------------------------

// The figure meshes' lifetime: the constructor body the figure classes share, Create and Clear, the classes
// 0x41 and 0x42's constructors, destructors and virtuals, the drawer and owner setters, and the figure
// manager's constructor and its CreateObject, which gives every new figure a drawer. See figure_mesh.h,
// figure_manager.h and the "Figure meshes" section of docs/notes/world.md.

// --- Construction -------------------------------------------------------------------------------

void FigureMesh::SetUpFigure() {
    refCount = 0;
    id = -1;
    prototypeId = 0;
    type = 0;
    flags = 0;
    owner = nullptr;
    localRotation.SetIdentity();
    worldRotation.SetIdentity();
    object = nullptr;
    std::construct_at(&offsets);
    std::construct_at(&colorsA);
    std::construct_at(&colorsB);
    std::memset(&box.bounds, 0, sizeof(box.bounds));
    box.pivot = {};
}

FigureMesh::FigureMesh() {
    SetUpFigure();
    CreateMesh();
}

void FigureMesh::CreateMesh() {
    radius = 0.0f;
    drawer = nullptr;
    flags = 0;
    materialValue = 0;
    meshType = 0;
    std::memset(&buffers, 0, sizeof(buffers));
    textureOffsetU = 0;
    textureOffsetV = 0;
    flatColour = 0xFFFFFFFF;
    flatSpecular = 0xFFFFFFFF;
    std::memset(&box.bounds, 0, sizeof(box.bounds));
    box.pivot = {};
}

// --- Clearing -----------------------------------------------------------------------------------

void FigureMesh::Clear() {
    name.clear();
    directory.clear();
    BuffersChanged();
    offsets.clear();
    colorsA.clear();
    colorsB.clear();
    std::memset(&buffers, 0, sizeof(buffers));
    materialValue = 0;
    meshType = 0;
    if (drawer != nullptr) {
        drawer->mesh = nullptr;
        drawer->Release();
    }
    drawer = nullptr;
    flags = 0;
}

void PlainFigureMesh::Destroy() {
    vertices.clear();
    normals.clear();
    corners.clear();
    words.clear();
    indices.clear();
    texCoords.clear();
    Clear();
}

void SharedFigureMesh::Destroy() {
    vertices.clear();
    normals.clear();
    Clear();
}

// --- Destruction --------------------------------------------------------------------------------

void FigureMesh::TearDownFigure() {
    Clear();
    lib::FreeStorage(colorsB);
    lib::FreeStorage(colorsA);
    lib::FreeStorage(offsets);
}

FigureMesh::~FigureMesh() { TearDownFigure(); }

// --- The classes 0x41 and 0x42 ---------------------------------------------------------------

SharedFigureMesh::SharedFigureMesh() {
}

SharedFigureMesh::~SharedFigureMesh() {
}

PlainFigureMesh::PlainFigureMesh() {
}

PlainFigureMesh::~PlainFigureMesh() {
}

// --- Small virtuals ----------------------------------------------------------------------------

void FigureMesh::Create() { CreateMesh(); }
void FigureMesh::Destroy() { Clear(); }
const char* FigureMesh::GetName() { return name.c_str(); }
const char* FigureMesh::ClassName() { return "CFigure"; }
ClassId FigureMesh::GetClassId() { return ClassId::FigureMesh; }

// --- Drawer and owner ---------------------------------------------------------------------------

void FigureMesh::SetDrawer(FigureMeshDrawer* newDrawer) {
    if (drawer != nullptr) {
        drawer->mesh = nullptr;
        drawer->Release();
    }
    drawer = newDrawer;
    if (newDrawer != nullptr) {
        newDrawer->mesh = this;
        drawer->AddRef();
    }
}

void FigureMesh::SetObject(TransformObject* newObject) { object = newObject; }

// --- The figure manager -------------------------------------------------------------------------

FigureManager::FigureManager() {}

EngineObject* FigureManager::NewObject(int objectType) {
    return static_cast<EngineObject*>(::CreateFigureObject(objectType));
}

EngineObject* FigureManager::CreateFigureObject(int newType, int newId) {
    EngineObject* object = BaseManager::CreateObject(newType, newId);
    const auto kind = static_cast<FigureObjectType>(newType);
    if (kind < FigureObjectType::SharedFigureMesh || kind > FigureObjectType::SkyObject || object == nullptr) return object;
    BaseManager* drawers = context->SpecificManager();
    auto* drawer = drawers->CreateObjectAs<FigureMeshDrawer>(SideObjectType::Drawer, -1);
    static_cast<FigureMesh*>(object)->SetDrawer(drawer);
    return object;
}
