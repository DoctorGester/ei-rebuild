#pragma once

// Figure meshes: the figure part objects created by the figure factory (the figure manager's types 0x41-0x44) and their
// loaders. See docs/notes/world.md, "Figure meshes".

#include "engine/core/core_transform.h"
#include "lib/vecmath.h"
#include "engine/scene/pipeline.h"
#include "engine/core/ref.h"

#include "lib/bit_flags.h"
#include "lib/containers.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

using TexCoord = render::TexCoord;
using MeshCorner = render::MeshCorner;

// What the renderer draws from (FigureMesh::buffers): pointers and counts of the mesh's arrays.
struct MeshBuffers {
    VertexPacket* vertices = nullptr;
    int vertexCount = 0;  // packets
    NormalPacket* normals = nullptr;
    int normalCount = 0;
    TexCoord* texCoords = nullptr;
    int texCoordCount = 0;
    MeshCorner* corners = nullptr;
    int cornerCount = 0;
    std::uint32_t* words = nullptr;  // 4-byte elements, unknown
    int wordCount = 0;
    std::uint16_t* indices = nullptr; // corner indices, 3 per triangle
    int indexCount = 0;
};

// The header in front of a FIG mesh: the magic ("FIG8" = 0x38474946 morphs) and the element counts, then two
// more dwords.
struct FigHeader {
    std::uint32_t magic = 0;
    int vertexCount = 0;
    int normalCount = 0;
    int texCoordCount = 0;
    int indexCount = 0;
    int cornerCount = 0;
    int wordCount = 0;
    std::uint32_t unused_1C = 0;
};
static_assert(sizeof(FigHeader) == 0x20);

// The FIG record format of a figure part (a figures.res file, or a part of a .lnk figure): a FigHeader, the material
// value and the mesh type (dwords), the bounds, the vertex packets, then the normals, texture coordinates,
// indices, corners and words. A FIG8 record holds 8 versions of every bound and vertex block (the corners of the
// figure's morph), another FIG one version; the older records have no FigHeader and hold 6 versions, with a count
// before every array.
namespace fig {

inline constexpr std::uint32_t kFigMagic = 0x474946;      // "FIG": the low three bytes of the magic
inline constexpr std::uint32_t kFig8Magic = 0x38474946;   // "FIG8"
inline constexpr int kFig8Versions = 8;
inline constexpr int kFigVersions = 1;
inline constexpr int kOldVersions = 6;

inline bool IsFig(std::uint32_t magic) { return (magic & 0xFFFFFF) == kFigMagic; }
inline int VersionsOf(std::uint32_t magic) { return magic == kFig8Magic ? kFig8Versions : kFigVersions; }

// The bounds: the centre, the box minimum, the box maximum (3 floats each) and the radius, per version.
inline constexpr int kBoundsFloats = 10;
constexpr std::size_t BoundsBytes(int versions) { return kBoundsFloats * versions * sizeof(float); }
// The material value and the mesh type, in front of the bounds (after the FigHeader of a FIG record).
inline constexpr std::size_t kMeshWordsBytes = 2 * sizeof(std::uint32_t);
// A FIG record up to its bounds.
inline constexpr std::size_t kHeaderBytes = sizeof(FigHeader) + kMeshWordsBytes;
// A vertex packet's blocks hold 4 floats, one set per version.
inline constexpr int kBlockFloats = 4;
// The older records' block of records between the texture coordinates and the corners that the loaders skip.
inline constexpr int kSkippedRecordBytes = 0x34;

// One element of a version (a float, or a Vector3 at 3 floats): `v` holds the versions one after the other.
template <typename T>
T Version(const float* v, int k) {
    if constexpr (std::is_same_v<T, float>) return v[k];
    else return T{v[3 * k], v[3 * k + 1], v[3 * k + 2]};
}

// A value morphed from 8 versions `v` (a float or Vector3 each) by the weights s, t, u:
// lerp(lerp(lerp(v0, v1, s), lerp(v2, v3, s), t), lerp(lerp(v4, v5, s), lerp(v6, v7, s), t), u).
template <typename T>
T MorphCube(const float* v, float s, float t, float u) {
    T lower = Lerp(Lerp(Version<T>(v, 0), Version<T>(v, 1), s), Lerp(Version<T>(v, 2), Version<T>(v, 3), s), t);
    T upper = Lerp(Lerp(Version<T>(v, 4), Version<T>(v, 5), s), Lerp(Version<T>(v, 6), Version<T>(v, 7), s), t);
    return Lerp(lower, upper, u);
}

// The older 6-version layout (a prism), also the bones' cage:
// lerp(lerp(lerp(c0, c1, s), c2, t), lerp(lerp(c3, c4, s), c5, t), u).
template <typename T>
T MorphPrism(const float* v, float s, float t, float u) {
    T lower = Lerp(Lerp(Version<T>(v, 0), Version<T>(v, 1), s), Version<T>(v, 2), t);
    T upper = Lerp(Lerp(Version<T>(v, 3), Version<T>(v, 4), s), Version<T>(v, 5), t);
    return Lerp(lower, upper, u);
}

} // namespace fig

class Object3D;
class FigureMesh;
class LinkMesh;
struct LinkPartRanges;

struct FigureMeshDrawer;
// The packets of `source` (`packets` of them) with the per-vertex `offsets` added (a vertex beyond the offsets keeps
// its position), in one work array that the next call reuses.
const VertexPacket* MovedPositions(const VertexPacket* source, int packets,
                                  const std::vector<Vector3>& offsets);

// The material a figure part is drawn with (the draw's fourth argument) is a Material: MaterialFlag::KeepSpecular
// keeps the speculars (FigureMeshDrawer::DrawLit), the lights scale colors.diffuse / colors.specular
// (Light::SetLitColors) and the GPU lighting adds colors.emissive (render::MeshLighting).
class Material;

class Light;

// The bits of EngineObject::flags on a figure mesh (a part of an object's figure).
enum class FigureMeshFlag : std::uint32_t {
    Stationary = 0x8,   // a part of a non-unit object's figure (Object3D::LoadFigures, the client's figure creation);
                        // nothing reads it
    Loaded = 0x20,      // LoadFromMemory / the figure manager's load ran
    Drawn = 0x40,       // drawn through drawer this frame (cleared first; its screen is where the part was last drawn)
    Hidden = 0x80,      // not drawn (nothing more, not even the finishing call): the hidden weapon parts
    Flat = 0x100,       // drawn in flatColour / flatSpecular instead of lit
};
template <> inline constexpr bool lib::kBitFlags<FigureMeshFlag> = true;

// The figure meshes ("CFigure"): the parts of an object's figure (Object3D::parts), owning all their arrays. The figure
// manager's types 0x41 (a loose part) and 0x42 (an attached part) are plain FigureMeshes; 0x43 UiMesh (ui_mesh.h) and
// 0x44 SkyObject (sky_object.h) derive from it.
class FigureMesh : public TransformObject {
public:
    FigureMesh();
    ~FigureMesh() override;

    // The create virtual of every figure class (the derived ones call it): no drawer, flags, buffers or bounds;
    // white flat colours.
    void Create() override;
    // The arrays emptied, then Clear.
    void Destroy() override;
    std::string_view GetName() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    // Sets the rotation, rotates the position by it (plus `offset`, copied to a member) and
    // rebuilds the rotation matrix from the stored quaternion.
    void UpdateWorld(const Quaternion& rotation, const Vector3& offset) override;
    // Loads the mesh from memory at *cursor (advanced past it), once (FigureMeshFlag::Loaded set afterwards): data whose
    // first dword starts with "FIG" through LoadFig, other data through LoadOld.
    void LoadFromMemory(char** cursor);

    // Points the buffer descriptor at the mesh's own arrays and sizes the colour arrays (one per normal).
    void RefreshBuffers();

    // Reads the bounding volume (centre, box, radius) that follows the mesh header: "FIG8" files hold 8 versions
    // of each value (the morph's corners; the first is taken), other versions one. Sets the box and centre of
    // `box.bounds`.
    void LoadBounds(char** cursor, std::uint32_t magic);

    // Fits box.bounds around the vertices; nothing without vertices.
    void FitVertexBounds();

    // Called for every part of a Object3D: grows the world-space box [min, max] by the 8 corners of this part's
    // box (box.bounds) transformed by its matrix; nothing when the position is 1e4 or more from the origin.
    void ExtendWorldBox(float* maxX, float* maxY, float* maxZ, float* minX, float* minY, float* minZ) const;

    // The mesh's buffers or vertices changed in place: the renderer builds its copy kept on the GPU
    // again (render::MeshChanged).
    void BuffersChanged();
    struct OffsetSpan {
        const Vector3* data = nullptr;
        int count = 0;
    };
    void SetOffsets(const OffsetSpan* offsets);

    // Names and colour arrays emptied, the buffer descriptor cleared, the drawer released, no flags.
    void Clear();
    // Lets the old drawer go (released) and takes `drawer` (one more reference, its mesh set).
    void SetDrawer(FigureMeshDrawer* drawer);
    void SetObject(TransformObject* owner);
    // The mesh's part in `figure` (its owner's, Object3D::figure) by the mesh's name, null when the figure has none;
    // found once per figure (figureRanges; figure_part_draw.cpp).
    const LinkPartRanges* PartIn(const LinkMesh* figure);

private:
    // A "FIG" mesh at *cursor, the first of FIG8's 8 versions of every bound and vertex (the meshes loaded here are
    // never morphed).
    void LoadFig(char** cursor);
    // The older format without a FIG header: 6 versions of the bounds and of every vertex (the first taken), each
    // array preceded by its count.
    void LoadOld(char** cursor);
    void SetUpFigure();

public:
    // EngineObject: flags the FigureMeshFlag bits; type the figure manager's type (0x41-0x44; the class id is
    // ClassId::FigureMesh). TransformObject: localPosition relative to the object (a bone places it),
    // worldPosition UpdateWorld's moved position, localRotation and worldRotation.
    // The part -> object matrix: the rotation, the local position as column 3 (the draws build it,
    // ObjectWorldMatrix in map_object_shadow.h).
    Matrix4 worldMatrix;
    Ref<FigureMeshDrawer> drawer;      // (SetDrawer)
    TransformObject* object = nullptr;  // the owner whose figure may draw the mesh: a Object3D, or an interface window (UiWindow)
    // PartIn's last answer, for the figure whose LinkMesh::serial is figureSerial (0: none yet).
    const LinkPartRanges* figureRanges = nullptr;
    std::uint64_t figureSerial = 0;
    MeshBuffers buffers;
    // passed to the draws after the buffers (as their bits): the texture offset (u, v) an animated mesh's
    // drawer sets per frame (FigureMeshDrawer::Animate); an interface sprite's frame of its sprite sheet.
    float textureOffsetU = 0.0f;
    float textureOffsetV = 0.0f;
    // every colour / specular of a flat mesh (FigureMeshFlag::Flat); set for every part of an object by
    // Object3D::SetPartsFlatColour / SetPartsFlatSpecular (the interface's model view).
    std::uint32_t flatColour = 0;
    std::uint32_t flatSpecular = 0;
    // the pose matrix (row-major): UpdateWorld's rotation, its translation row the moved position.
    Matrix4 poseMatrix;
    std::string name;
    std::string directory;              // where its .fig file is (NewFigure empties it)
    std::vector<Vector3> offsets;      // per-vertex offsets added when drawing
    std::vector<std::uint32_t> colorsA;    // one per normal (lit colours)
    std::vector<std::uint32_t> colorsB;    // one per normal
    render::MeshSlots meshSlots;           // the renderer's copies on the GPU (render::MeshDraw::slots)
    // from the file header (before meshType): the material value of the mesh (the number in its
    // material group's name). An object's part takes both from its piece's LinkPartHeader (Object3D::LoadPart).
    std::uint32_t materialValue = 0;
    std::uint32_t meshType = 0;         // from the file header; 13..16: animated
    Vector3 center;
    Vector3 boxMin;
    Vector3 boxMax;
    float radius = 0.0f;
    // the part's box: a FootprintPart, copied whole from the piece's LinkPartHeader
    // (Object3D::LoadPart). Flags 4 loaded, 0x400 "BASE", 0x90 "CROWN" / "EMPTY", 0x800 "EMPTY"; the centre
    // is the piece's place in the figure, the radius the blood's size.
    FootprintPart box;                  // bounds and the part's position when it was loaded (cleared with the bounds)
    std::vector<VertexPacket> vertices;
    std::vector<NormalPacket> normals;
    std::vector<TexCoord> texCoords;
    std::vector<MeshCorner> corners;
    std::vector<std::uint32_t> words;
    std::vector<std::uint16_t> indices;
};

// The figure loader's bounding volume of a FIG figure part: `data` holds the centre, box minimum, box maximum
// and radius, 8 versions each for FIG8 (blended trilinearly by the weights s, t and u) or one otherwise.
void MorphFigureBounds(ClipBounds* out, const float* data, float s, float t, float u, std::uint32_t magic);
// The same for figure parts without a FIG header: 6 corner versions of each value, blended as the bones are.
void MorphFigureBounds6(ClipBounds* out, const float* data, float s, float t, float u);
