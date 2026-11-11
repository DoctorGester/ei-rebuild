// Figure mesh loaders: the FIG mesh data of a figure part, and the figure loader's bounding volume morphed by the
// figure's three morph coefficients. See docs/notes/world.md ("Figure meshes").
#include "game/models/figure_mesh.h"

#include "game/core/core_maths.h"
#include "game/core/core_transform.h"
#include "game/models/figure_manager.h"
#include "game/models/link_figure.h"
#include "game/core/manager_context.h"
#include "game/scene/pipeline.h"
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

template <typename T>
T Read(char*& p) {
    const T v = lib::Load<T>(p);
    p += sizeof(T);
    return v;
}

// Reads the first of `versions` copies of a value at the cursor (a byte copy, floats too) and moves past all of them.
template <typename T>
void ReadFirstVersion(T& value, char** cursor, int versions) {
    static_assert(lib::kFormatValue<T>, "plain bytes without padding, one size in every build");
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
    static_assert(lib::kFormatValue<T>, "plain bytes without padding, one size in every build");
    array.resize(static_cast<std::size_t>(count));
    if (count != 0) {
        std::memcpy(array.data(), *cursor, static_cast<std::size_t>(count) * sizeof(T));
        *cursor += count * static_cast<int>(sizeof(T));
    }
}

int ReadCount(char** cursor) {
    int n = lib::Load<int>(*cursor);
    *cursor += 4;
    return n;
}

// The limit on the squared distance of the position from the origin (the float 1e8).
inline constexpr float kFarLimit = 100000000.0f;

// Grows [min, max] by one transformed point. The original selects with the comparison's below-or-unordered bit
// as a mask on the float bits, so a NaN on either side takes the point.
void Extend(float& max, float& min, float v) {
    max = MaxKeepB(max, v);
    min = MinKeepA(v, min);
}

} // namespace

void FigureMesh::LoadBounds(char** cursor, std::uint32_t magic) {
    const int versions = fig::VersionsOf(magic);
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
    if (magic == fig::kFig8Magic) {
        // The centre, box minimum and box maximum are 8 versions of 3 floats each, the radius 8 floats.
        constexpr int kVector = 3 * fig::kFig8Versions;
        Vector3 c = fig::MorphCube<Vector3>(data, s, t, u);
        Vector3 mn = fig::MorphCube<Vector3>(data + kVector, s, t, u);
        Vector3 mx = fig::MorphCube<Vector3>(data + 2 * kVector, s, t, u);
        float radius = fig::MorphCube<float>(data + 3 * kVector, s, t, u);
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
    constexpr int kVector = 3 * fig::kOldVersions;
    Vector3 c = fig::MorphPrism<Vector3>(data, s, t, u);
    Vector3 mn = fig::MorphPrism<Vector3>(data + kVector, s, t, u);
    Vector3 mx = fig::MorphPrism<Vector3>(data + 2 * kVector, s, t, u);
    float radius = fig::MorphPrism<float>(data + 3 * kVector, s, t, u);
    out->center = c;
    out->flags |= 1;
    out->radius = radius;
    out->SetBox(mn, mx);
}

void FigureMesh::LoadFig(char** cursor) {
    FigHeader header = lib::Load<FigHeader>(*cursor);
    *cursor += sizeof(header);
    materialValue = Read<std::uint32_t>(*cursor);
    meshType = Read<std::uint32_t>(*cursor);
    LoadBounds(cursor, header.magic);

    vertices.resize(header.vertexCount);
    LoadVertices(vertices, cursor, fig::VersionsOf(header.magic));
    if (!vertices.empty()) box.bounds.Fit(vertices.data(), lib::Count(vertices));

    LoadArray(normals, cursor, header.normalCount);
    LoadArray(texCoords, cursor, header.texCoordCount);
    LoadArray(indices, cursor, header.indexCount);
    LoadArray(corners, cursor, header.cornerCount);
    LoadArray(words, cursor, header.wordCount);

    RefreshBuffers();
    flags |= FigureMeshFlag::Loaded;
}

// ---- Old (non-FIG) format -----------------------------------------------------------------------

void FigureMesh::LoadOld(char** cursor) {
    materialValue = Read<std::uint32_t>(*cursor);
    meshType = Read<std::uint32_t>(*cursor);
    ClipBounds& bounds = box.bounds;
    ReadFirstVersion(bounds.center, cursor, fig::kOldVersions);
    ReadFirstVersion(bounds.boxMin, cursor, fig::kOldVersions);
    ReadFirstVersion(bounds.boxMax, cursor, fig::kOldVersions);
    ReadFirstVersion(bounds.radius, cursor, fig::kOldVersions);
    bounds.flags |= BoundsFlag::Sphere;
    bounds.flags |= BoundsFlag::Box;

    vertices.resize(ReadCount(cursor));
    LoadVertices(vertices, cursor, fig::kOldVersions);
    if (!vertices.empty()) bounds.Fit(vertices.data(), lib::Count(vertices));

    // The second colour array of the records is opaque black.
    LoadArray(normals, cursor, ReadCount(cursor));
    colorsB.assign(normals.size() * 4, 0xFF000000);
    LoadArray(texCoords, cursor, ReadCount(cursor));
    const int skipped = ReadCount(cursor);
    *cursor += skipped * fig::kSkippedRecordBytes;
    LoadArray(corners, cursor, ReadCount(cursor));
    LoadArray(words, cursor, ReadCount(cursor));
    LoadArray(indices, cursor, ReadCount(cursor));

    RefreshBuffers();
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

// ---- Buffers, vertex offsets ---------------------------------------------------------

void FigureMesh::FitVertexBounds() {
    if (!vertices.empty()) box.bounds.Fit(vertices.data(), lib::Count(vertices));
}

void FigureMesh::RefreshBuffers() {
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
    // The positions the renderer keeps on the GPU move with new offsets: the animation's frame.
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
    std::uint32_t magic = lib::Load<std::uint32_t>(*cursor);
    if (fig::IsFig(magic)) LoadFig(cursor);
    else LoadOld(cursor);
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

// The figure meshes' lifetime: the constructor body the figure classes share, Create, Clear and Destroy, the drawer
// and owner setters, and the figure manager's constructor and its CreateObject, which gives every new figure a drawer. See figure_mesh.h,
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
    FigureMesh::Create();
}

void FigureMesh::Create() {
    radius = 0.0f;
    SetDrawer(nullptr);
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
    figureRanges = nullptr;
    figureSerial = 0;
    directory.clear();
    BuffersChanged();
    offsets.clear();
    colorsA.clear();
    colorsB.clear();
    std::memset(&buffers, 0, sizeof(buffers));
    materialValue = 0;
    meshType = 0;
    SetDrawer(nullptr);
    flags = 0;
}

void FigureMesh::Destroy() {
    vertices.clear();
    normals.clear();
    corners.clear();
    words.clear();
    indices.clear();
    texCoords.clear();
    Clear();
}

// --- Destruction --------------------------------------------------------------------------------

FigureMesh::~FigureMesh() { Clear(); }

// --- Small virtuals ----------------------------------------------------------------------------

std::string_view FigureMesh::GetName() { return name; }
const char* FigureMesh::ClassName() { return "CFigure"; }
ClassId FigureMesh::GetClassId() { return ClassId::FigureMesh; }

// --- Drawer and owner ---------------------------------------------------------------------------

void FigureMesh::SetDrawer(FigureMeshDrawer* newDrawer) {
    if (drawer) drawer->mesh = nullptr;
    drawer.Assign(newDrawer);
    if (newDrawer) newDrawer->mesh = this;
}

void FigureMesh::SetObject(TransformObject* newObject) { object = newObject; }

// --- The figure manager -------------------------------------------------------------------------

FigureManager::FigureManager() {}

EngineObject* FigureManager::NewObject(int objectType) {
    return CreateFigureObject(objectType);
}

EngineObject* FigureManager::CreateObject(int newType, int newId) {
    EngineObject* object = BaseManager::CreateObject(newType, newId);
    const auto kind = static_cast<FigureObjectType>(newType);
    if (kind < FigureObjectType::LoosePart || kind > FigureObjectType::SkyObject || object == nullptr) return object;
    BaseManager* drawers = context->SpecificManager();
    auto* drawer = drawers->CreateObjectAs<FigureMeshDrawer>(SideObjectType::Drawer, -1);
    static_cast<FigureMesh*>(object)->SetDrawer(drawer);
    return object;
}
