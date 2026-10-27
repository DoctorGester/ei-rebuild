#pragma once

// Figure meshes: the game-side figure part objects created by the figure factory (class ids 0x41-0x44) and
// their loaders. Names provisional; see the "Figure meshes" section of docs/notes/world.md.

#include "core_transform.h"
#include "maths.h"
#include "model.h"
#include "raster_target.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

// Four vertex positions in SoA form for the transform library: x[4], y[4], z[4] and w[4] (the
// loaders set w to 1.0). Vertex i of a mesh is component i & 3 of packet i >> 2.
// 0x40 bytes. Also the moved positions the part drawers build. `block` is
// four rows of lanes, not a matrix (check_cleanup skips the name).
struct VertexPacket {
    float block[4][4];
};
static_assert(sizeof(VertexPacket) == 0x40);

// Four normals in the same SoA form (mirrored together with the positions); the loaders copy them verbatim.
struct NormalPacket {
    std::uint32_t w[16];
};
// A texture coordinate (u, v); the rasteriser scales them to fixed point.
struct TexCoord {
    std::uint32_t w[2];
};
// MeshCorner (a triangle corner): model.h.
static_assert(sizeof(NormalPacket) == 0x40 && sizeof(TexCoord) == 8 && sizeof(MeshCorner) == 6);

// What the renderer draws from (+0x9C): pointers and counts of the mesh's arrays (class 0x41 points
// the last four into the loaded file data instead).
struct MeshBuffers {
    void* vertices;     // +0x00 (+0x9C) VertexPacket
    int vertexCount;    //               packets
    void* normals;      // +0x08 (+0xA4) NormalPacket
    int normalCount;
    void* texCoords;    // +0x10 (+0xAC) TexCoord
    int texCoordCount;
    void* corners;      // +0x18 (+0xB4) MeshCorner
    int cornerCount;
    void* words;        // +0x20 (+0xBC) 4-byte elements, unknown
    int wordCount;
    void* indices;      // +0x28 (+0xC4) uint16 corner indices, 3 per triangle
    int indexCount;
};
static_assert(sizeof(MeshBuffers) == 0x30);

class CObject3D;   // map_object_figures.h
class CBaseManager;   // base_manager.h
class CFigureMesh;
class CPixelConverter;   // texture.h

struct FigureMeshDrawer;   // link_figure.h
// The flags an animated mesh's drawer gets on top of its own (figure_part_draw*.cpp).
inline constexpr std::uint32_t kAnimatedDrawerFlags = /*00790C0C*/ 0x200;

// The material a figure part is drawn with (the draw's fourth argument) is a CMaterial (material.h): flags_
// 0x10 keeps the speculars (draw flag 8), the lights scale colors.diffuse / colors.specular
// (CLight::SetLitColors) and the transform object lights with colors.emissive's first three dwords.
class CMaterial;

class CLight;   // spell.h

// The lights a figure part is lit with ({data, count}; the draw's third argument).
struct FigureLights {
    CLight** data;
    int count;
};

// The figure meshes' base ("CFigure"): the layout the classes below share (0x1E0 bytes, class 0x41's object;
// class 0x42 adds four arrays, CPlainFigureMesh). A CTransformObject (core_transform.h: the engine object's
// fields, the local and world position and rotation). The parts of an object's figure (CObject3D::parts) are
// these. A real virtual class: the classes 0x41 (CSharedFigureMesh), 0x42 (CPlainFigureMesh), 0x43 (CUiMesh,
// ui.h) and 0x44 (CSkyObject, sky_object.h) have C++ constructors and the compiler's vtables. Abstract: two
// virtuals are the derived classes'.
class CFigureMesh : public CTransformObject {
public:
    // The overridden virtuals; the others are CTransformObject's.
    int Create() override;                 // CreateMesh (the derived classes' Create jumps to it)
    void Destroy() override;               // Clear
    const char* GetName() override;        // name_
    const char* GetClassName() override;   // "CFigure"
    int GetClassId() override;             // 0x44
    // figure_mesh_file.cpp: sets the rotation, rotates the position by it (plus `offset`, copied to a member) and
    // rebuilds the rotation matrix from the stored quaternion.
    void UpdateWorld(const Quaternion& rotation, const Vector3& offset) override;
    // figure_mesh_file.cpp: writes the mesh to an MFC CFile: the name (length then characters), each buffer
    // (buffers_) as a count and its elements, a 0, then centre, box, radius and two trailing dwords.
    // CPlainFigureMesh::LoadTransform reads it back.
    void SaveTransform(mfc::CFileVirtuals* file) override;
    // Copies another mesh's transform, name, the shared buffer
    // descriptors (texture coordinates, corners, words, indices), +0xDC and the bounds.
    virtual void CopyFigure(const CFigureMesh* other);
    // LoadFromMemory (classes 0x42-0x44 / 0x41, byte-identical; figure_manager.cpp). Loads the mesh from memory at
    // *cursor once (flags_ 0x20 set afterwards): data whose first dword starts with "FIG" through the FIG
    // loader, other data through the older-format one. (The abstract base's own is LoadNothing below.)
    virtual void LoadFromMemory(char** cursor, float s, float t, float u);
    virtual void LoadFig(char** cursor, float s, float t, float u) = 0;   // 20: "FIG" data
    virtual void LoadOld(char** cursor, float s, float t, float u) = 0;   // 21: the older format
    // The deleting destructors free into the game's heap.

    // Reads the bounding volume (centre, box, radius) that follows the mesh header and morphs it: "FIG8" files
    // hold 8 corner versions of each value, blended trilinearly by the figure's three morph coefficients; other
    // versions hold one. Sets the box and centre of `bounds_`.
    void LoadBounds(char** cursor, float s, float t, float u, std::uint32_t magic);

    // figure_mesh_life.cpp. The abstract base has no mesh to read: only marks itself loaded (flags_ 0x20).
    void LoadNothing(char** cursor, float s, float t, float u);
    // figure_mesh.cpp (the interface's item models). Fits bounds_ around the vertices (the transform pipeline's
    // fit); nothing without vertices.
    void FitVertexBounds();

    // Called for every part of a CObject3D: grows the world-space box [min, max] by the 8 corners of this part's
    // box (bounds_) transformed by its matrix; nothing when the position is 1e4 or more from the origin.
    void ExtendWorldBox(float* maxX, float* maxY, float* maxZ, float* minX, float* minY, float* minZ) const;

    // The mesh's buffers or vertices changed in place: the new renderer builds its copy kept on the GPU
    // again (render::MeshChanged).
    void BuffersChanged();
    // Copies `offsets` (any {Vector3* data; int count} pair) into offsets_.
    struct OffsetSpan {
        const Vector3* data;
        int count;
    };
    void SetOffsets(const OffsetSpan* offsets);
    // Projects the vertices flat through `pipeline`'s transform object (ProjectFlat: `matrix`, the points out, the
    // bounds grown) with the vertices moved by offsets_ when there are any (into a shared buffer). Returns the
    // vertex count (4 per packet).
    int DrawOffset(CTransformPipeline* pipeline, const float* matrix, Vector2* points, float* bounds);
    // figure_rasterise.cpp (the shadow pass): the projected `points` rasterised into `target` through
    // `rasteriser`, each triangle with its texture coordinates scaled to 0..255 (keyed when the target has key
    // texels); returns the number of points used (4 per vertex).
    int Rasterise(CPixelConverter* rasteriser, const RasterTriangle::Point* points, RasterTarget* target);

    // Lifetime (figure_mesh_life.cpp). CreateMesh, the create virtual of every figure class: no drawer, flags,
    // buffers or bounds; white flat colours. Returns 1.
    int CreateMesh();
    // Clear: names and colour arrays emptied, the buffer descriptor cleared, the drawer let go (released), no
    // flags.
    void Clear();
    // Lets the old drawer go (released) and takes `drawer` (one more reference,
    // its mesh_ set).
    void SetDrawer(FigureMeshDrawer* drawer);
    // The object whose part the mesh is (object_).
    void SetObject(CObject3D* object);

protected:
    // The constructor the classes 0x41-0x43 share (the names are members): the transform object's fields, no
    // owner, empty arrays, no bounds (SetUpFigure), then CreateMesh.
    CFigureMesh();
    // The deleting destructor: TearDownFigure (Clear, the colour and offset arrays), then the names and the
    // engine object's destructor.
    ~CFigureMesh() override;

private:
    // The constructor's work: the engine object's fields, identity
    // rotations, no object, empty offset and colour arrays, no bounds (the names are constructed
    // before it).
    void SetUpFigure();
    // The destructors' shared work: Clear, then the colour and offset arrays freed.
    void TearDownFigure();

public:

    // CEngineObject: flags_ 0x20 loaded, 0x40 drawn through drawer_ (its screen_ is where the part was last
    // drawn), 0x80 hidden; type_ the class id (0x41-0x44). CTransformObject: localPosition_ relative to the
    // object (a bone places it), worldPosition_ UpdateWorld's moved position, localRotation_ and worldRotation_.
    // The part -> object matrix: the rotation, the local position as column 3 (the draws build it,
    // ObjectWorldMatrix in map_object_shadow.h).
    Matrix4 worldMatrix_;
    FigureMeshDrawer* drawer_;
    CObject3D* object_;                 // +0x098 the object whose figure (+0xF0) may draw the mesh
    MeshBuffers buffers_;               // +0x09C
    // +0x0CC / +0x0D0 passed to the draws after the buffers (as their bits): the texture offset
    // (u, v) an animated mesh's drawer sets per frame (FigureMeshDrawer::Animate); an interface
    // sprite's frame of its sprite sheet (CUiMesh, ui.h).
    float textureOffsetU_;
    float textureOffsetV_;
    // +0x0D4 / +0x0D8 every colour / specular of a flat mesh (flags_ 0x100); set for every part of
    // an object by CObject3D::SetPartsFlatColour / SetPartsFlatSpecular (the interface's model view).
    std::uint32_t flatColour_;
    std::uint32_t flatSpecular_;
    std::uint32_t spareDC_;             // +0x0DC cleared by Create, copied by CopyFrom; nothing reads it
    // +0x0E0 the pose matrix (row-major): UpdateWorld's rotation, its translation row (+0x110) the
    // moved position.
    Matrix4 poseMatrix_;
    mfc::CString name_;                 // +0x120 (the scene loader names it after its mesh)
    mfc::CString directory_;            // where its .fig file is (NewFigure empties it)
    lib::AlignedArray<Vector3> offsets_;     // +0x128 per-vertex offsets added when drawing
    lib::AlignedArray<std::uint32_t> colorsA_;   // +0x138 one per normal (lit colours)
    lib::AlignedArray<std::uint32_t> colorsB_;   // +0x148 one per normal
    // +0x158 from the file header (before meshType_): the material value of the scene's mesh
    // (SceneMesh::materialValue, the number in its material group's name; scene_registry.cpp). An
    // object's part takes both from its piece's LinkPartHeader (CObject3D::LoadPart).
    std::uint32_t materialValue_;
    std::uint32_t meshType_;            // +0x15C from the file header; 13..16: animated
    Vector3 center_;                    // +0x160
    Vector3 boxMin_;                    // +0x16C
    Vector3 boxMax_;                    // +0x178
    float radius_;                      // +0x184
    // +0x188 the part's box: with boxPivot_ a FootprintPart (ai_map.h), copied whole from the
    // piece's LinkPartHeader (CObject3D::LoadPart). Flags 4 loaded, 0x400 "BASE", 0x90 "CROWN" /
    // "EMPTY", 0x800 "EMPTY"; the centre is the piece's place in the figure, the radius the blood's
    // size.
    ClipBounds bounds_;
    Vector3 boxPivot_;                  // +0x1B4 the part's position when it was loaded; cleared with the bounds
    lib::AlignedArray<VertexPacket> vertices_;     // +0x1C0
    lib::AlignedArray<NormalPacket> normals_;    // +0x1D0
};
static_assert(offsetof(CFigureMesh, flags_) == 0x08 && offsetof(CFigureMesh, localRotation_) == 0x34);
static_assert(offsetof(CFigureMesh, buffers_) == 0x9C);
static_assert(offsetof(CFigureMesh, drawer_) == 0x94 && offsetof(CFigureMesh, object_) == 0x98);
static_assert(offsetof(CFigureMesh, textureOffsetU_) == 0xCC && offsetof(CFigureMesh, flatSpecular_) == 0xD8);
static_assert(offsetof(CFigureMesh, worldPosition_) == 0x28 && offsetof(CFigureMesh, poseMatrix_) == 0xE0);
static_assert(offsetof(CFigureMesh, name_) == 0x120 && offsetof(CFigureMesh, meshType_) == 0x15C);
static_assert(offsetof(CFigureMesh, colorsA_) == 0x138);
static_assert(offsetof(CFigureMesh, materialValue_) == 0x158 && offsetof(CFigureMesh, worldMatrix_) == 0x54);
static_assert(offsetof(CFigureMesh, center_) == 0x160);
static_assert(offsetof(CFigureMesh, radius_) == 0x184 && offsetof(CFigureMesh, boxPivot_) == 0x1B4);
static_assert(offsetof(CFigureMesh, bounds_) == 0x188);
static_assert(offsetof(CFigureMesh, vertices_) == 0x1C0);
static_assert(sizeof(CFigureMesh) == 0x1E0);

// Class 0x41 (22 virtuals; 0x1E0 bytes): a part whose last four buffers point into the loaded file data (the
// figure manager's parts that are not attached).
class CSharedFigureMesh : public CFigureMesh {
public:
    // The figure factory's class 0x41 (inlined there): CFigureMesh's constructor, then the vertices and normals
    // empty.
    CSharedFigureMesh();
    // The normals and vertices freed, then CFigureMesh's destructor.
    ~CSharedFigureMesh() override;

    // The virtuals: CFigureMesh's but for the deleting destructor, Clear and the FIG loader (Create jumps to
    // CreateMesh). Clear (figure_mesh_life.cpp): the vertices and normals emptied, then CFigureMesh's Clear.
    void Destroy() override;
    // The FIG loader (figure_mesh.cpp). CPlainFigureMesh's, but only the vertices and records are copied; the other
    // buffers point into the file data.
    void LoadFig(char** cursor, float s, float t, float u) override;
    // The older-format loader (figure_mesh.cpp). CPlainFigureMesh's, the last four buffers pointing into the data.
    void LoadOld(char** cursor, float s, float t, float u) override;
};

// Class 0x42 (24 virtuals; 0x220 bytes): a mesh that owns all its arrays (the figure manager's attached parts,
// the scene's figure records). CUiMesh (class 0x43, ui.h) and CSkyObject (0x44, sky_object.h) derive from it.
class CPlainFigureMesh : public CFigureMesh {
public:
    // The figure factory's class 0x42 and the scene's records inline it:
    // CFigureMesh's constructor, then the six arrays empty.
    CPlainFigureMesh();
    // The six arrays freed (last first), then CFigureMesh's destructor.
    ~CPlainFigureMesh() override;

    // The virtuals: CFigureMesh's but for the deleting destructor, Clear, the file reader and the four loaders
    // and mirrors (Create jumps to CreateMesh). Clear (figure_mesh_life.cpp): the mesh's own arrays emptied, then
    // CFigureMesh's Clear.
    void Destroy() override;
    // figure_mesh_file.cpp: reads the mesh SaveTransform wrote.
    void LoadTransform(mfc::CFileVirtuals* file) override;
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

    // Points the buffer descriptor at the mesh's own arrays and sizes the colour arrays
    // (one per normal).
    void RefreshBuffers();

    lib::AlignedArray<MeshCorner> corners_;       // +0x1E0
    lib::AlignedArray<std::uint32_t> words_;      // +0x1F0
    lib::AlignedArray<std::uint16_t> indices_;    // +0x200
    lib::AlignedArray<TexCoord> texCoords_;       // +0x210
};
static_assert(offsetof(CPlainFigureMesh, corners_) == 0x1E0 && offsetof(CPlainFigureMesh, texCoords_) == 0x210);
// The figure factory allocates the game's sizes (figure_factory.cpp).
static_assert(sizeof(CSharedFigureMesh) == 0x1E0 && sizeof(CPlainFigureMesh) == 0x220);

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
void __stdcall MorphFigureBounds(ClipBounds* out, const float* data, float s, float t, float u, std::uint32_t magic);
// The same for figure parts without a FIG header: 6 corner versions of each value, blended as the bones are.
void __stdcall MorphFigureBounds6(ClipBounds* out, const float* data, float s, float t, float u);
