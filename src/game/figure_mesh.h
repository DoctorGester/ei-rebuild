#pragma once

// Figure meshes: the figure part objects created by the figure factory (class ids 0x41-0x44) and their
// loaders. See docs/notes/world.md, "Figure meshes".

#include "core_transform.h"
#include "maths.h"
#include "model.h"
#include "raster_target.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#undef GetClassName   // CEngineObject::GetClassName

// Four vertex positions in SoA form for the transform library: x[4], y[4], z[4] and w[4] (the loaders set w to
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

// What the renderer draws from (CFigureMesh::buffers_): pointers and counts of the mesh's arrays (class 0x41
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
static_assert(sizeof(MeshBuffers) == 0x30);

class CObject3D;
class CFigureMesh;
class CPixelConverter;

struct FigureMeshDrawer;
// The flags an animated mesh's drawer gets on top of its own (figure_part_draw*.cpp).
inline constexpr std::uint32_t kAnimatedDrawerFlags = /*00790C0C*/ 0x200;

// The material a figure part is drawn with (the draw's fourth argument) is a CMaterial: flags_
// 0x10 keeps the speculars (draw flag 8), the lights scale colors.diffuse / colors.specular
// (CLight::SetLitColors) and the transform object lights with colors.emissive's first three dwords.
class CMaterial;

class CLight;

// The figure meshes' base ("CFigure"): the parts of an object's figure (CObject3D::parts). Derived: 0x41
// CSharedFigureMesh, 0x42 CPlainFigureMesh, 0x43 CUiMesh (ui.h), 0x44 CSkyObject (sky_object.h).
class CFigureMesh : public CTransformObject {
public:
    int Create() override;                 // CreateMesh (the derived classes' Create jumps to it)
    void Destroy() override;               // Clear
    const char* GetName() override;
    const char* GetClassName() override;
    int GetClassId() override;             // 0x44
    // Sets the rotation, rotates the position by it (plus `offset`, copied to a member) and
    // rebuilds the rotation matrix from the stored quaternion.
    void UpdateWorld(const Quaternion& rotation, const Vector3& offset) override;
    // Writes the mesh to a file: the name (length then characters), each buffer
    // (buffers_) as a count and its elements, a 0, then centre, box, radius and two trailing dwords.
    // CPlainFigureMesh::LoadTransform reads it back.
    void SaveTransform(lib::File* file) override;
    // Copies another mesh's transform, name, the shared buffer descriptors (texture coordinates, corners,
    // words, indices), spareDC_ and the bounds.
    virtual void CopyFigure(const CFigureMesh* other);
    // Loads the mesh from memory at *cursor once (flags_ 0x20 set afterwards): data whose first dword starts
    // with "FIG" through LoadFig, other data through LoadOld.
    virtual void LoadFromMemory(char** cursor, float s, float t, float u);
    virtual void LoadFig(char** cursor, float s, float t, float u) = 0;   // "FIG" data
    virtual void LoadOld(char** cursor, float s, float t, float u) = 0;   // the older format
    // The deleting destructors free into the game's heap.

    // Reads the bounding volume (centre, box, radius) that follows the mesh header and morphs it: "FIG8" files
    // hold 8 corner versions of each value, blended trilinearly by the figure's three morph coefficients; other
    // versions hold one. Sets the box and centre of `box_.bounds`.
    void LoadBounds(char** cursor, float s, float t, float u, std::uint32_t magic);

    // Fits box_.bounds around the vertices; nothing without vertices.
    void FitVertexBounds();

    // Called for every part of a CObject3D: grows the world-space box [min, max] by the 8 corners of this part's
    // box (box_.bounds) transformed by its matrix; nothing when the position is 1e4 or more from the origin.
    void ExtendWorldBox(float* maxX, float* maxY, float* maxZ, float* minX, float* minY, float* minZ) const;

    // The mesh's buffers or vertices changed in place: the new renderer builds its copy kept on the GPU
    // again (render::MeshChanged).
    void BuffersChanged();
    struct OffsetSpan {
        const Vector3* data;
        int count;
    };
    void SetOffsets(const OffsetSpan* offsets);
    // Projects the vertices flat through `pipeline`'s transform object (ProjectFlat: `matrix`, the points out, the
    // bounds grown) with the vertices moved by offsets_ when there are any (into a shared buffer). Returns the
    // vertex count (4 per packet).
    int DrawOffset(CTransformPipeline* pipeline, const float* matrix, Vector2* points, float* bounds);
    // The shadow pass: the projected `points` rasterised into `target` through `rasteriser`, each triangle with
    // its texture coordinates scaled to 0..255 (keyed when the target has key texels); returns the number of
    // points used (4 per vertex).
    int Rasterise(CPixelConverter* rasteriser, const RasterTriangle::Point* points, RasterTarget* target);

    // The create virtual of every figure class: no drawer, flags, buffers or bounds; white flat colours.
    // Returns 1.
    int CreateMesh();
    // Names and colour arrays emptied, the buffer descriptor cleared, the drawer released, no flags.
    void Clear();
    // Lets the old drawer go (released) and takes `drawer` (one more reference, its mesh_ set).
    void SetDrawer(FigureMeshDrawer* drawer);
    void SetObject(CTransformObject* owner);

protected:
    CFigureMesh();
    ~CFigureMesh() override;

private:
    void SetUpFigure();
    void TearDownFigure();

public:

    // CEngineObject: flags_ 0x20 loaded, 0x40 drawn through drawer_ (its screen_ is where the part was last
    // drawn), 0x80 hidden; type_ the class id (0x41-0x44). CTransformObject: localPosition_ relative to the
    // object (a bone places it), worldPosition_ UpdateWorld's moved position, localRotation_ and worldRotation_.
    // The part -> object matrix: the rotation, the local position as column 3 (the draws build it,
    // ObjectWorldMatrix in map_object_shadow.h).
    Matrix4 worldMatrix_;
    FigureMeshDrawer* drawer_;
    CTransformObject* object_;          // the owner whose figure may draw the mesh: a CObject3D, or an interface window (CUiWindow)
    MeshBuffers buffers_;
    // passed to the draws after the buffers (as their bits): the texture offset (u, v) an animated mesh's
    // drawer sets per frame (FigureMeshDrawer::Animate); an interface sprite's frame of its sprite sheet.
    float textureOffsetU_;
    float textureOffsetV_;
    // every colour / specular of a flat mesh (flags_ 0x100); set for every part of an object by
    // CObject3D::SetPartsFlatColour / SetPartsFlatSpecular (the interface's model view).
    std::uint32_t flatColour_;
    std::uint32_t flatSpecular_;
    std::uint32_t spareDC_;             // cleared by Create, copied by CopyFrom; nothing reads it
    // the pose matrix (row-major): UpdateWorld's rotation, its translation row the moved position.
    Matrix4 poseMatrix_;
    std::string name_;
    std::string directory_;             // where its .fig file is (NewFigure empties it)
    std::vector<Vector3> offsets_;     // per-vertex offsets added when drawing
    std::vector<std::uint32_t> colorsA_;   // one per normal (lit colours)
    std::vector<std::uint32_t> colorsB_;   // one per normal
    // from the file header (before meshType_): the material value of the mesh (the number in its
    // material group's name). An object's part takes both from its piece's LinkPartHeader (CObject3D::LoadPart).
    std::uint32_t materialValue_;
    std::uint32_t meshType_;            // from the file header; 13..16: animated
    Vector3 center_;
    Vector3 boxMin_;
    Vector3 boxMax_;
    float radius_;
    // the part's box: a FootprintPart, copied whole from the piece's LinkPartHeader
    // (CObject3D::LoadPart). Flags 4 loaded, 0x400 "BASE", 0x90 "CROWN" / "EMPTY", 0x800 "EMPTY"; the centre
    // is the piece's place in the figure, the radius the blood's size.
    FootprintPart box_;                 // bounds_ and the part's position when it was loaded (cleared with the bounds)
    std::vector<VertexPacket> vertices_;
    std::vector<NormalPacket> normals_;
};
static_assert(offsetof(CFigureMesh, flags_) == 0x08 && offsetof(CFigureMesh, localRotation_) == 0x34);
static_assert(offsetof(CFigureMesh, buffers_) == 0x9C);
static_assert(offsetof(CFigureMesh, drawer_) == 0x94 && offsetof(CFigureMesh, object_) == 0x98);
static_assert(offsetof(CFigureMesh, textureOffsetU_) == 0xCC && offsetof(CFigureMesh, flatSpecular_) == 0xD8);
static_assert(offsetof(CFigureMesh, worldPosition_) == 0x28 && offsetof(CFigureMesh, poseMatrix_) == 0xE0);

// Class 0x41: a part whose last four buffers point into the loaded file data (the figure manager's parts that
// are not attached).
class CSharedFigureMesh : public CFigureMesh {
public:
    CSharedFigureMesh();
    ~CSharedFigureMesh() override;

    // Clear: the vertices and normals emptied, then CFigureMesh's Clear.
    void Destroy() override;
    // The FIG loader. CPlainFigureMesh's, but only the vertices and records are copied; the other
    // buffers point into the file data.
    void LoadFig(char** cursor, float s, float t, float u) override;
    // The older-format loader. CPlainFigureMesh's, the last four buffers pointing into the data.
    void LoadOld(char** cursor, float s, float t, float u) override;
};

// Class 0x42: a mesh that owns all its arrays (the figure manager's attached parts). CUiMesh (class 0x43, ui.h) and CSkyObject (0x44, sky_object.h) derive from it.
class CPlainFigureMesh : public CFigureMesh {
public:
    CPlainFigureMesh();
    ~CPlainFigureMesh() override;

    // Clear: the mesh's own arrays emptied, then CFigureMesh's Clear.
    void Destroy() override;
    void LoadTransform(lib::File* file) override;
    // Loads a "FIG" mesh from memory at *cursor (advanced past it), morphing FIG8 vertices; once only (flag
    // 0x20).
    void LoadFig(char** cursor, float s, float t, float u) override;
    // The older format without a FIG header, chosen by the LoadFromMemory virtual when the data does not start
    // with "FIG": 6 corner versions of the bounds and of every vertex, each array preceded by its count.
    void LoadOld(char** cursor, float s, float t, float u) override;
    // Mirror the mesh in x / y (negates that component of every vertex and normal) and swap the first two corners
    // of every triangle.
    virtual void MirrorX();
    virtual void MirrorY();

    // Points the buffer descriptor at the mesh's own arrays and sizes the colour arrays (one per normal).
    void RefreshBuffers();

    std::vector<MeshCorner> corners_;
    std::vector<std::uint32_t> words_;
    std::vector<std::uint16_t> indices_;
    std::vector<TexCoord> texCoords_;
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
// and radius, 8 versions each for FIG8 (morphed as in CFigureMesh::LoadBounds, except that the radius keeps
// its lower blend on the FPU) or one otherwise. The loader argument is not used.
void MorphFigureBounds(ClipBounds* out, const float* data, float s, float t, float u, std::uint32_t magic);
// The same for figure parts without a FIG header: 6 corner versions of each value, blended as the bones are.
void MorphFigureBounds6(ClipBounds* out, const float* data, float s, float t, float u);
