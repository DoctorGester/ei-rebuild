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
#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/float_select.h"
#include "lib/vecmath.h"
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

Vector3 Load(const float* p) { return {p[0], p[1], p[2]}; }

// One morphed vector from 8 corner versions (`v` points at 8 Vector3s):
// lerp(lerp(lerp(v0, v1, s), lerp(v2, v3, s), t), lerp(lerp(v4, v5, s), lerp(v6, v7, s), t), u).
Vector3 MorphVector(const float* v, float s, float t, float u) {
    Vector3 lower = Lerp(Lerp(Load(v), Load(v + 3), s), Lerp(Load(v + 6), Load(v + 9), s), t);
    Vector3 upper = Lerp(Lerp(Load(v + 12), Load(v + 15), s), Lerp(Load(v + 18), Load(v + 21), s), t);
    return Lerp(lower, upper, u);
}

// The older 6-corner layout (non-FIG files), as for the bones (six corners per bone):
// lerp(lerp(lerp(c0, c1, s), c2, t), lerp(lerp(c3, c4, s), c5, t), u).
Vector3 MorphVector6(const float* c, float s, float t, float u) {
    Vector3 lower = Lerp(Lerp(Load(c), Load(c + 3), s), Load(c + 6), t);
    Vector3 upper = Lerp(Lerp(Load(c + 9), Load(c + 12), s), Load(c + 15), t);
    return Lerp(lower, upper, u);
}

// A scalar from 6 corners, as MorphVector6.
float MorphScalar6(const float* r, float s, float t, float u) {
    return Lerp(Lerp(Lerp(r[0], r[1], s), r[2], t), Lerp(Lerp(r[3], r[4], s), r[5], t), u);
}

// A scalar morphed from 8 versions, as MorphVector.
float MorphScalar(const float* r, float s, float t, float u) {
    return Lerp(Lerp(Lerp(r[0], r[1], s), Lerp(r[2], r[3], s), t), Lerp(Lerp(r[4], r[5], s), Lerp(r[6], r[7], s), t), u);
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

void FigureMesh::LoadBounds(char** cursor, std::uint32_t magic) {
    const int versions = magic == kFig8Magic ? kFig8Versions : 1;
    ReadFirstVersion(center, cursor, versions);
    ReadFirstVersion(boxMin, cursor, versions);
    ReadFirstVersion(boxMax, cursor, versions);
    ReadFirstVersion(radius, cursor, versions);
    box.bounds.center = center;
    box.bounds.flags |= BoundsFlag::Sphere;
    box.bounds.radius = radius;
    box.bounds.SetBox(boxMin, boxMax);
}

void MorphFigureBounds(ClipBounds* out, const float* data, float s, float t, float u, std::uint32_t magic) {
    Vector3 boxMin, boxMax;
    if (magic == kFig8Magic) {
        Vector3 c = MorphVector(data, s, t, u);
        Vector3 mn = MorphVector(data + 0x18, s, t, u);
        Vector3 mx = MorphVector(data + 0x30, s, t, u);
        float radius = MorphScalar(data + 0x48, s, t, u);
        out->flags |= 1;
        out->center = c;
        out->radius = radius;
        boxMin = mn;
        boxMax = mx;
    } else {
        std::memcpy(&out->center, data, 12);
        std::memcpy(&boxMin, data + 3, 12);
        std::memcpy(&boxMax, data + 6, 12);
        out->radius = data[9];
        out->flags |= 1;
    }
    out->SetBox(boxMin, boxMax);
}

void MorphFigureBounds6(ClipBounds* out, const float* data, float s, float t, float u) {
    Vector3 c = MorphVector6(data, s, t, u);
    Vector3 mn = MorphVector6(data + 0x12, s, t, u);
    Vector3 mx = MorphVector6(data + 0x24, s, t, u);
    float radius = MorphScalar6(data + 0x36, s, t, u);
    out->center = c;
    out->flags |= 1;
    out->radius = radius;
    out->SetBox(mn, mx);
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
    LoadArray(texCoords, cursor, header.texCoordCount);
    LoadArray(indices, cursor, header.indexCount);
    LoadArray(corners, cursor, header.cornerCount);
    LoadArray(words, cursor, header.wordCount);
    buffers.texCoords = texCoords.data();
    buffers.texCoordCount = header.texCoordCount;
    buffers.indices = indices.data();
    buffers.indexCount = header.indexCount;
    buffers.corners = corners.data();
    buffers.cornerCount = header.cornerCount;
    buffers.words = words.data();
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
    LoadArray(texCoords, cursor, ReadCount(cursor));
    SkipRecords34(cursor);
    LoadArray(corners, cursor, ReadCount(cursor));
    LoadArray(words, cursor, ReadCount(cursor));
    LoadArray(indices, cursor, ReadCount(cursor));
    buffers.texCoords = texCoords.data();
    buffers.texCoordCount = lib::Count(texCoords);
    buffers.corners = corners.data();
    buffers.cornerCount = lib::Count(corners);
    buffers.words = words.data();
    buffers.wordCount = lib::Count(words);
    buffers.indices = indices.data();
    buffers.indexCount = lib::Count(indices);

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
    // The squared distance from the figure's origin; NaN passes.
    float distance = LengthSquared(localPosition);
    if (distance > kFarLimit) return;

    Matrix4 m = WorldMatrix();

    // The 8 corners of the box (centre + minimum / maximum), maximum x first, times the matrix as
    // a row vector.
    const Vector3& c = box.bounds.center;
    const Vector3& lo = box.bounds.boxMin;
    const Vector3& hi = box.bounds.boxMax;
    for (int corner = 0; corner < 8; ++corner) {
        Vector3 p = {((corner & 4) ? lo : hi).x + c.x, ((corner & 2) ? lo : hi).y + c.y, ((corner & 1) ? lo : hi).z + c.z};
        Vector3 w = TransformRow(m, p);
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

// ---- Pose --------------------------------------------------------------------------------

void FigureMesh::UpdateWorld(const Quaternion& rotation, const Vector3& offset) {
    // The pose virtual stores the quaternion as the world rotation.
    CombineRotation(rotation);
    // The position rotated by the quaternion, then moved by `offset`.
    worldPosition = rotation.Rotate(localPosition) + offset;

    poseMatrix = WorldMatrix();
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
    texCoords.clear();
    indices.clear();
    corners.clear();
    words.clear();
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
    return ::CreateFigureObject(objectType);
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
