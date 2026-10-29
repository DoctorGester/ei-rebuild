#pragma once

// Figure meshes: the figure part objects created by the figure factory (the figure manager's types 0x41-0x44) and their
// loaders. See docs/notes/world.md, "Figure meshes".

#include "core_transform.h"
#include "maths.h"
#include "model.h"

#include "lib/bit_flags.h"
#include "lib/containers.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Four vertex positions in SoA form for the projections: x[4], y[4], z[4] and w[4] (the loaders set w to
// 1.0). Vertex i of a mesh is component i & 3 of packet i >> 2. Also the moved positions the part drawers
// build. `block` is four rows of lanes, not a matrix (check_cleanup skips the name).
struct VertexPacket {
    float block[4][4];
    // The rows by name (the interface sprites' quads: the four corners' x, y, z (0) and w (1)).
    float (&x())[4] { return block[0]; }
    float (&y())[4] { return block[1]; }
    float (&z())[4] { return block[2]; }
    float (&w())[4] { return block[3]; }
    const float (&x() const)[4] { return block[0]; }
    const float (&y() const)[4] { return block[1]; }
    const float (&z() const)[4] { return block[2]; }
    const float (&w() const)[4] { return block[3]; }
};
static_assert(sizeof(VertexPacket) == 0x40);

// Four normals in the same SoA form (mirrored together with the positions); the loaders copy them verbatim.
struct NormalPacket {
    float block[4][4];
};
// A texture coordinate (u, v); the rasteriser scales them to fixed point.
struct TexCoord {
    float u, v;
};
static_assert(sizeof(NormalPacket) == 0x40 && sizeof(TexCoord) == 8 && sizeof(MeshCorner) == 6);

// What the renderer draws from (FigureMesh::buffers): pointers and counts of the mesh's arrays (type 0x41
// points the last four into the loaded file data instead).
struct MeshBuffers {
    VertexPacket* vertices;
    int vertexCount;    // packets
    NormalPacket* normals;
    int normalCount;
    TexCoord* texCoords;
    int texCoordCount;
    MeshCorner* corners;
    int cornerCount;
    std::uint32_t* words;   // 4-byte elements, unknown
    int wordCount;
    std::uint16_t* indices; // corner indices, 3 per triangle
    int indexCount;
};

class CObject3D;
class FigureMesh;

struct FigureMeshDrawer;
// The flags an animated mesh's drawer gets on top of its own (figure_part_draw*.cpp).
inline constexpr std::uint32_t kAnimatedDrawerFlags = /*00790C0C*/ 0x200;

// The material a figure part is drawn with (the draw's fourth argument) is a Material: MaterialFlag::KeepSpecular
// keeps the speculars (draw flag 8), the lights scale colors.diffuse / colors.specular
// (CLight::SetLitColors) and the GPU lighting adds colors.emissive (render::MeshLighting).
class Material;

class CLight;

// The bits of EngineObject::flags on a figure mesh (a part of an object's figure).
enum class FigureMeshFlag : std::uint32_t {
    Stationary = 0x8,   // a part of a non-unit object's figure (CObject3D::LoadFigures, the client's figure creation);
                        // nothing reads it
    Lit = 0x10,         // the colours are lit and kept: the draw lights the part only without it (the sky mesh has it
                        // from the start); a non-static object's draw clears it on every part
    Loaded = 0x20,      // LoadFromMemory / the figure manager's load ran
    Drawn = 0x40,       // drawn through drawer this frame (cleared first; its screen is where the part was last drawn)
    Hidden = 0x80,      // not drawn (nothing more, not even the finishing call): the hidden weapon parts
    Flat = 0x100,       // drawn in flatColour / flatSpecular instead of lit
};
template <> inline constexpr bool lib::kBitFlags<FigureMeshFlag> = true;

// The figure meshes' base ("CFigure"): the parts of an object's figure (CObject3D::parts). Derived (by the figure manager's type): 0x41
// SharedFigureMesh, 0x42 PlainFigureMesh, 0x43 UiMesh (ui.h), 0x44 SkyObject (sky_object.h).
class FigureMesh : public TransformObject {
public:
    void Create() override;                 // CreateMesh (the derived classes' Create jumps to it)
    void Destroy() override;               // Clear
    const char* GetName() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    // Sets the rotation, rotates the position by it (plus `offset`, copied to a member) and
    // rebuilds the rotation matrix from the stored quaternion.
    void UpdateWorld(const Quaternion& rotation, const Vector3& offset) override;
    // Writes the mesh to a file: the name (length then characters), each buffer
    // (buffers) as a count and its elements, a 0, then centre, box, radius and two trailing dwords.
    // PlainFigureMesh::LoadTransform reads it back.
    void SaveTransform(lib::File* file) override;
    // Copies another mesh's transform, name, the shared buffer descriptors (texture coordinates, corners,
    // words, indices) and the bounds.
    virtual void CopyFigure(const FigureMesh* other);
    // Loads the mesh from memory at *cursor once (FigureMeshFlag::Loaded set afterwards): data whose first dword starts
    // with "FIG" through LoadFig, other data through LoadOld.
    virtual void LoadFromMemory(char** cursor);
    virtual void LoadFig(char** cursor) = 0;   // "FIG" data
    virtual void LoadOld(char** cursor) = 0;   // the older format
    // The deleting destructors free into the game's heap.

    // Reads the bounding volume (centre, box, radius) that follows the mesh header: "FIG8" files hold 8 versions
    // of each value (the morph's corners; the first is taken), other versions one. Sets the box and centre of
    // `box.bounds`.
    void LoadBounds(char** cursor, std::uint32_t magic);

    // Fits box.bounds around the vertices; nothing without vertices.
    void FitVertexBounds();

    // Called for every part of a CObject3D: grows the world-space box [min, max] by the 8 corners of this part's
    // box (box.bounds) transformed by its matrix; nothing when the position is 1e4 or more from the origin.
    void ExtendWorldBox(float* maxX, float* maxY, float* maxZ, float* minX, float* minY, float* minZ) const;

    // The mesh's buffers or vertices changed in place: the new renderer builds its copy kept on the GPU
    // again (render::MeshChanged).
    void BuffersChanged();
    struct OffsetSpan {
        const Vector3* data;
        int count;
    };
    void SetOffsets(const OffsetSpan* offsets);

    // The create virtual of every figure class: no drawer, flags, buffers or bounds; white flat colours.
    // Returns 1.
    void CreateMesh();
    // Names and colour arrays emptied, the buffer descriptor cleared, the drawer released, no flags.
    void Clear();
    // Lets the old drawer go (released) and takes `drawer` (one more reference, its mesh set).
    void SetDrawer(FigureMeshDrawer* drawer);
    void SetObject(TransformObject* owner);

protected:
    FigureMesh();
    ~FigureMesh() override;

private:
    void SetUpFigure();
    void TearDownFigure();

public:

    // EngineObject: flags the FigureMeshFlag bits; type the figure manager's type (0x41-0x44; the class id is
    // ClassId::FigureMesh). TransformObject: localPosition relative to the object (a bone places it),
    // worldPosition UpdateWorld's moved position, localRotation and worldRotation.
    // The part -> object matrix: the rotation, the local position as column 3 (the draws build it,
    // ObjectWorldMatrix in map_object_shadow.h).
    Matrix4 worldMatrix;
    FigureMeshDrawer* drawer;
    TransformObject* object;           // the owner whose figure may draw the mesh: a CObject3D, or an interface window (UiWindow)
    MeshBuffers buffers;
    // passed to the draws after the buffers (as their bits): the texture offset (u, v) an animated mesh's
    // drawer sets per frame (FigureMeshDrawer::Animate); an interface sprite's frame of its sprite sheet.
    float textureOffsetU;
    float textureOffsetV;
    // every colour / specular of a flat mesh (FigureMeshFlag::Flat); set for every part of an object by
    // CObject3D::SetPartsFlatColour / SetPartsFlatSpecular (the interface's model view).
    std::uint32_t flatColour;
    std::uint32_t flatSpecular;
    // the pose matrix (row-major): UpdateWorld's rotation, its translation row the moved position.
    Matrix4 poseMatrix;
    std::string name;
    std::string directory;              // where its .fig file is (NewFigure empties it)
    std::vector<Vector3> offsets;      // per-vertex offsets added when drawing
    std::vector<std::uint32_t> colorsA;    // one per normal (lit colours)
    std::vector<std::uint32_t> colorsB;    // one per normal
    // from the file header (before meshType): the material value of the mesh (the number in its
    // material group's name). An object's part takes both from its piece's LinkPartHeader (CObject3D::LoadPart).
    std::uint32_t materialValue;
    std::uint32_t meshType;             // from the file header; 13..16: animated
    Vector3 center;
    Vector3 boxMin;
    Vector3 boxMax;
    float radius;
    // the part's box: a FootprintPart, copied whole from the piece's LinkPartHeader
    // (CObject3D::LoadPart). Flags 4 loaded, 0x400 "BASE", 0x90 "CROWN" / "EMPTY", 0x800 "EMPTY"; the centre
    // is the piece's place in the figure, the radius the blood's size.
    FootprintPart box;                  // bounds and the part's position when it was loaded (cleared with the bounds)
    std::vector<VertexPacket> vertices;
    std::vector<NormalPacket> normals;
};

// Figure type 0x41: a part whose last four buffers point into the loaded file data (the figure manager's parts that
// are not attached).
class SharedFigureMesh : public FigureMesh {
public:
    SharedFigureMesh();
    ~SharedFigureMesh() override;

    // Clear: the vertices and normals emptied, then FigureMesh's Clear.
    void Destroy() override;
    // The FIG loader. PlainFigureMesh's, but only the vertices and records are copied; the other
    // buffers point into the file data.
    void LoadFig(char** cursor) override;
    // The older-format loader. PlainFigureMesh's, the last four buffers pointing into the data.
    void LoadOld(char** cursor) override;
};

// Figure type 0x42: a mesh that owns all its arrays (the figure manager's attached parts). UiMesh (type 0x43, class id
// 0x46, ui.h) and SkyObject (type 0x44, class id 0x20, sky_object.h) derive from it.
class PlainFigureMesh : public FigureMesh {
public:
    PlainFigureMesh();
    ~PlainFigureMesh() override;

    // Clear: the mesh's own arrays emptied, then FigureMesh's Clear.
    void Destroy() override;
    void LoadTransform(lib::File* file) override;
    // Loads a "FIG" mesh from memory at *cursor (advanced past it), the first of FIG8's 8 versions of every vertex; once only (flag
    // 0x20).
    void LoadFig(char** cursor) override;
    // The older format without a FIG header, chosen by the LoadFromMemory virtual when the data does not start
    // with "FIG": 6 versions of the bounds and of every vertex (the first taken), each array preceded by its count.
    void LoadOld(char** cursor) override;
    // Mirror the mesh in x / y (negates that component of every vertex and normal) and swap the first two corners
    // of every triangle.
    virtual void MirrorX();
    virtual void MirrorY();

    // Points the buffer descriptor at the mesh's own arrays and sizes the colour arrays (one per normal).
    void RefreshBuffers();

    std::vector<MeshCorner> corners;
    std::vector<std::uint32_t> words;
    std::vector<std::uint16_t> indices;
    std::vector<TexCoord> texCoords;
};
// The figure factory allocates the game's sizes.

// The header in front of a FIG mesh: the magic ("FIG8" = 0x38474946 morphs) and the element counts, then two
// more dwords.
struct FigHeader {
    std::uint32_t magic;
    int vertexCount;
    int normalCount;
    int texCoordCount;
    int indexCount;
    int cornerCount;
    int wordCount;
    std::uint32_t unused_1C;
};
static_assert(sizeof(FigHeader) == 0x20);

constexpr std::uint32_t kFig8Magic = 0x38474946;   // "FIG8"

// The figure loader's bounding volume of a FIG figure part: `data` holds the centre, box minimum, box maximum
// and radius, 8 versions each for FIG8 (blended trilinearly by the weights s, t and u) or one otherwise.
void MorphFigureBounds(ClipBounds* out, const float* data, float s, float t, float u, std::uint32_t magic);
// The same for figure parts without a FIG header: 6 corner versions of each value, blended as the bones are.
void MorphFigureBounds6(ClipBounds* out, const float* data, float s, float t, float u);
