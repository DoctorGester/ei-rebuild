#pragma once

// Figure meshes: the game-side figure part objects created by the figure factory 005AA8E0
// (class ids 0x41-0x44) and their loaders (0x005A0000-0x005B0000). Names provisional; see the
// "Figure meshes" section of docs/notes/world.md.

#include "core_transform.h"
#include "maths.h"
#include "model.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

// Four vertex positions in SoA form for the transform library: x[4], y[4], z[4] and w[4] (the
// loaders set w to 1.0). Vertex i of a mesh is component i & 3 of packet i >> 2 (005A0CD0).
// 0x40 bytes. Also the moved positions the part drawers build (007C2E48 / 007C2E60). `block` is
// four rows of lanes, not a matrix (check_cleanup skips the name).
struct VertexPacket {
    float block[4][4];
};
static_assert(sizeof(VertexPacket) == 0x40);

// Four normals in the same SoA form (mirrored together with the positions by 005A4B30 /
// 005A4BF0); the loaders copy them verbatim.
struct NormalPacket {
    std::uint32_t w[16];
};
// A texture coordinate (u, v); 005A0E80 scales them to fixed point.
struct TexCoord {
    std::uint32_t w[2];
};
// A triangle corner: position index, a second index (normal?) and texture coordinate index.
struct MeshCorner {
    std::uint16_t index[3];
};
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

struct FigureMeshDrawer;   // link_figure.h

// The material a figure part is drawn with (005A7A70's fourth argument) is a CMaterial (material.h):
// flags_ 0x10 keeps the speculars (draw flag 8), the lights scale colors.diffuse / colors.specular
// (CLight::SetLitColors) and the transform object lights with colors.emissive's first three dwords.
class CMaterial;

class CLight;   // spell.h

// The lights a figure part is lit with ({data, count}; 005A7A70's third argument).
struct FigureLights {
    CLight** data;
    int count;
};

// The figure meshes' base ("CFigure"): the layout of the class 0x42 mesh (0x220 bytes), which the
// classes below share. A CTransformObject (core_transform.h: the engine object's fields, the local
// and world position and rotation). The parts of an object's figure (CObject3D::parts) are these.
// A real virtual class (C19): the classes 0x41 (CSharedFigureMesh), 0x42 (CPlainFigureMesh), 0x43
// (CUiMesh, ui.h) and 0x44 (CSkyObject, sky_object.h) have C++ constructors and the compiler's
// vtables. The slot bodies are plain members registered by address (Create's is CreateMesh,
// CopyFigure's CopyFigureImpl, ...); the virtuals call them. Abstract: slots 20 and 21 are the
// derived classes'. game.exe's base vtable 0073C224 (20 slots) stays: only the registered bodies
// ConstructBase / DestructBase store it, which no code of ours calls (they go with C20).
class CFigureMesh : public CTransformObject {
public:
    static constexpr std::uintptr_t kBaseVtableAddress = std::uintptr_t(0x0073C224);   // the base

    // The vtable (game.exe's base 0073C224 and the pure slots 20 and 21). The slots not listed are
    // CTransformObject's (1, 4-6, 8-10, 13, 14, 17).
    int Create() override;                 // 2: 0059F210 CreateMesh (the derived classes' 005A6C70 / 005A6E00 jump to it)
    void Destroy() override;               // 3: 0059F2B0 Clear
    const char* GetName() override;        // 7: 00453440 (name_)
    const char* GetClassName() override;   // 11: 00453450 "CFigure"
    int GetClassId() override;             // 12: 00453460 (0x44)
    // 15: 0059F510 SetPose. 16: 0059F710 SaveToFile (the mesh written to an MFC CFile).
    void UpdateWorld(const Quaternion& parentRotation, const Vector3& parentPosition) override;
    void SaveTransform(void* file) override;
    virtual void CopyFigure(const CFigureMesh* other);   // 18: 0059F0F0 CopyFigureImpl
    // 19: 005A2480 LoadNothing (the meshes' is LoadFromMemoryImpl).
    virtual void LoadFromMemory(char** cursor, float s, float t, float u);
    virtual void LoadFig(char** cursor, float s, float t, float u) = 0;   // 20: "FIG" data
    virtual void LoadOld(char** cursor, float s, float t, float u) = 0;   // 21: the older format
    // The deleting destructors (slot 0) free into the game's heap.
    static void operator delete(void* block) { gamecrt::operator_delete(block); }

    // 005A18E0. Reads the bounding volume (centre, box, radius) that follows the mesh header and
    // morphs it: "FIG8" files hold 8 corner versions of each value, blended trilinearly by the
    // figure's three morph coefficients; other versions hold one. Sets the box and centre of
    // `bounds_`. RET 0x14.
    void LoadBounds(char** cursor, float s, float t, float u, std::uint32_t magic);

    // 005A2490 / 005A4CF0 (vtable slot 19 of classes 0x42-0x44 / 0x41, byte-identical; RET 0x10;
    // figure_manager.cpp, A-W12). Loads the mesh from memory at *cursor once (flags_ 0x20 set
    // afterwards): data whose first dword starts with "FIG" through slot 20, other data through
    // slot 21.
    void LoadFromMemoryImpl(char** cursor, float s, float t, float u);
    // 005A2480 (slot 19 of the base vtable 0073C224, class 0x44; RET 0x10; figure_mesh_life.cpp).
    // The base has no mesh to read: only marks itself loaded (flags_ 0x20).
    void LoadNothing(char** cursor, float s, float t, float u);
    // 005A37C0 (the interface's item models, 005EBFB0; figure_mesh.cpp). Fits bounds_ around the
    // vertices (the transform pipeline's slot 7); nothing without vertices.
    void FitVertexBounds();

    // 005A2500 (vtable slot 20 of classes 0x42-0x44). Loads a "FIG" mesh from memory at *cursor
    // (advanced past it), morphing FIG8 vertices; once only (flag 0x20). RET 0x10.
    void LoadFigImpl(char** cursor, float s, float t, float u);
    // 005A5C70 (slot 20 of class 0x41): the same, but only the vertices and records are copied;
    // the other buffers point into the file data.
    void LoadFigShared(char** cursor, float s, float t, float u);
    // 005A3800 (slot 21 of classes 0x42-0x44): the older format without a FIG header, chosen by
    // slot 19 (005A2490) when the data does not start with "FIG": 6 corner versions of the
    // bounds and of every vertex, each array preceded by its count. RET 0x10.
    void LoadOldImpl(char** cursor, float s, float t, float u);
    // 005A4D60 (slot 21 of class 0x41): the same, the last four buffers pointing into the data.
    void LoadOldShared(char** cursor, float s, float t, float u);

    // 0059FDF0 (called by 00518100 for every part of a CObject3D): grows the world-space box
    // [min, max] by the 8 corners of this part's box (bounds_) transformed by its matrix
    // (00401570); nothing when the position is 1e4 or more from the origin. RET 0x18.
    void ExtendWorldBox(float* maxX, float* maxY, float* maxZ, float* minX, float* minY, float* minZ) const;

    // 005A4B30 (vtable slot 22) / 005A4BF0 (slot 23): mirror the mesh in x / y (negates that
    // component of every vertex and normal) and swap the first two corners of every triangle.
    void MirrorXImpl();
    void MirrorYImpl();
    // 005A6D40: points the buffer descriptor at the mesh's own arrays and sizes the colour arrays
    // (one per normal).
    void RefreshBuffers();
    // The mesh's buffers or vertices changed in place: the new renderer builds its copy kept on the GPU
    // again (render::MeshChanged, R16d).
    void BuffersChanged();
    // 005A0B10: copies `offsets` (any {Vector3* data; int count} pair) into offsets_. RET 4.
    struct OffsetSpan {
        const Vector3* data;
        int count;
    };
    void SetOffsets(const OffsetSpan* offsets);
    // 005A0CD0: projects the vertices flat through `pipeline`'s transform object (ProjectFlat,
    // slot 29: `matrix`, the points out, the bounds grown) with the vertices moved by offsets_ when
    // there are any (into the shared buffer at 007C2E18). Returns the vertex count (4 per packet).
    // RET 0x10.
    int DrawOffset(CTransformPipeline* pipeline, const float* matrix, Vector2* points, float* bounds);
    // 0059F710 / 0059F8F0 (vtable slots, RET 4; figure_mesh_file.cpp): write the mesh to / read it
    // from an MFC CFile: the name (+0x120, length then characters), each array as a count and its
    // elements, a 0, then centre, box, radius and +0x158 / +0x15C.
    void SaveToFile(mfc::CFileVirtuals* file);
    void LoadFromFile(mfc::CFileVirtuals* file);
    // 0059F0F0 (slot 18, RET 4): copies another mesh's transform (00401490), name, the shared
    // buffer descriptors (texture coordinates, corners, words, indices), +0xDC and the bounds.
    void CopyFigureImpl(const CFigureMesh* other);
    // 0059F510 (slot 15, CTransformObject::UpdateWorld's place; RET 8): sets the rotation (slot 14
    // with `rotation`), rotates the position (+0x1C) by it into +0x28 (+ `offset`, copied to +0x110)
    // and rebuilds the rotation matrix at +0xE0 from the stored quaternion (+0x44).
    void SetPose(const Quaternion& rotation, const Vector3& offset);

    // Lifetime (figure_mesh_life.cpp, A-W13).
    // 005AABC0. The constructor body the classes 0x41-0x43 share in the original (the factory, the
    // scene's figure records): the transform object's fields, no owner, empty names and arrays, no
    // bounds, vtable 0073C224, then Create. Returns this. No code of ours calls it (the protected
    // constructor below does its work).
    CFigureMesh* ConstructBase();
    // 0059F210 (slot 2 of every figure class; 005A6C70 / 005A6E00 jump to it). No drawer, flags,
    // buffers or bounds; white flat colours. Returns 1.
    int CreateMesh();
    // 0059F2B0 (slot 3 of vtable 0073C224): names and colour arrays emptied, the buffer descriptor
    // cleared, the drawer let go (released), no flags.
    void Clear();
    // 005A6C80 (slot 3 of classes 0x42-0x44) / 005A6E10 (class 0x41): the mesh's own arrays freed,
    // then Clear.
    void ClearMesh();
    void ClearSharedMesh();
    // 0059F030. The destructor body: Clear, the colour and offset arrays, the names, the engine
    // object's destructor. No code of ours calls it (the destructor below does its work).
    void DestructBase();
    // 0059F390 (RET 4). Lets the old drawer go (released) and takes `drawer` (one more reference,
    // its mesh_ set).
    void SetDrawer(FigureMeshDrawer* drawer);
    // 0059F3D0 (RET 4). The object whose part the mesh is (object_).
    void SetObject(CObject3D* object);

protected:
    // 005AABC0 as C++ (the names constructed as members): SetUpFigure, then CreateMesh.
    CFigureMesh();
    // 0059F030's work without the vtables: TearDownFigure, then the names (members) and the engine
    // object's destructor.
    ~CFigureMesh() override;

private:
    // The constructor's and ConstructBase's shared work: the engine object's fields, identity
    // rotations, no object, empty offset and colour arrays, no bounds (the names are constructed
    // before it).
    void SetUpFigure();
    // The destructors' shared work: Clear, then the colour and offset arrays freed.
    void TearDownFigure();

public:

    // CEngineObject: flags_ 0x20 loaded, 0x40 drawn through drawer_ (its screen_ is where the part
    // was last drawn), 0x80 hidden; type_ the class id (0x41-0x44). CTransformObject: localPosition_
    // relative to the object (a bone places it, 0050C230), worldPosition_ SetPose's moved position,
    // localRotation_ and worldRotation_.
    // +0x054 part -> object: the rotation, the local position as column 3 (the draws build it,
    // ObjectWorldMatrix in map_object_shadow.h).
    Matrix4 worldMatrix_;
    FigureMeshDrawer* drawer_;          // +0x094 (00501D00 draws through it)
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
    // +0x0E0 the pose matrix (row-major): SetPose's rotation, its translation row (+0x110) the
    // moved position.
    Matrix4 poseMatrix_;
    mfc::CString name_;                 // +0x120 (the scene loader names it after its mesh)
    mfc::CString directory_;            // +0x124 where its .fig file is (005AC2F0; NewFigure empties it)
    lib::AlignedArray<Vector3> offsets_;     // +0x128 per-vertex offsets added when drawing (005A0B10 / 005A0CD0)
    lib::AlignedArray<std::uint32_t> colorsA_;   // +0x138 one per normal (lit colours)
    lib::AlignedArray<std::uint32_t> colorsB_;   // +0x148 one per normal
    // +0x158 from the file header (before meshType_): the material value of the scene's mesh
    // (SceneMesh::materialValue, the number in its material group's name; scene_registry.cpp). An
    // object's part takes both from its piece's LinkPartHeader (CObject3D::LoadPart).
    std::uint32_t materialValue_;
    std::uint32_t meshType_;            // +0x15C from the file header; 13..16: animated (005A7A70)
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
    // Class 0x41 ends here (0x1E0 bytes).
    lib::AlignedArray<MeshCorner> corners_;  // +0x1E0
    lib::AlignedArray<std::uint32_t> words_;     // +0x1F0
    lib::AlignedArray<std::uint16_t> indices_;    // +0x200
    lib::AlignedArray<TexCoord> texCoords_;       // +0x210
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
static_assert(offsetof(CFigureMesh, corners_) == 0x1E0);
static_assert(offsetof(CFigureMesh, texCoords_) == 0x210);
static_assert(sizeof(CFigureMesh) == 0x220);

// Class 0x41 (game.exe's vtable 0073FBF0, retired; 22 slots): a part whose last four buffers point
// into the loaded file data (the figure manager's parts that are not attached). The object is
// 0x1E0 bytes (the factory allocates that much): CFigureMesh's layout up to corners_, which it does
// not have; its constructor and destructor touch nothing past vertices_ and normals_ (the members
// after them are trivial).
class CSharedFigureMesh : public CFigureMesh {
public:
    // The figure factory's class 0x41 (005AA8E0 inlines it): CFigureMesh's constructor, then the
    // vertices and normals empty.
    CSharedFigureMesh();
    // 005AADC0's work: the normals and vertices freed, then CFigureMesh's destructor.
    ~CSharedFigureMesh() override;

    // The vtable: CFigureMesh's but for 0, 3 and 19-21 (slot 2, 005A6E00, jumps to CreateMesh).
    void Destroy() override;                                                // 3: 005A6E10 ClearSharedMesh
    void LoadFromMemory(char** cursor, float s, float t, float u) override; // 19: 005A4CF0 LoadFromMemoryImpl
    void LoadFig(char** cursor, float s, float t, float u) override;        // 20: 005A5C70 LoadFigShared
    void LoadOld(char** cursor, float s, float t, float u) override;        // 21: 005A4D60 LoadOldShared

    // The registrations (C20 retires them): 005AADC0 the destructor, 005AADA0 (slot 0) the
    // destructor, then operator delete when bit 0 of `flags` is set (returns this).
    void Destruct();
    CSharedFigureMesh* DeletingDestruct(unsigned flags);
};

// Class 0x42 (game.exe's vtable 0073C1C4, retired; 24 slots): a mesh that owns all its arrays (the
// figure manager's attached parts, the scene's figure records, 00453240). CUiMesh (class 0x43,
// ui.h) and CSkyObject (0x44, sky_object.h) derive from it.
class CPlainFigureMesh : public CFigureMesh {
public:
    // The figure factory's class 0x42 (005AA8E0) and the scene's records (00453240) inline it:
    // CFigureMesh's constructor, then the six arrays empty.
    CPlainFigureMesh();
    // 00454510's work: the six arrays freed (last first), then CFigureMesh's destructor.
    ~CPlainFigureMesh() override;

    // The vtable: CFigureMesh's but for 0, 3, 17 and 19-23 (slot 2, 005A6C70, jumps to CreateMesh).
    void Destroy() override;                                                // 3: 005A6C80 ClearMesh
    void LoadTransform(void* file) override;                                // 17: 0059F8F0 LoadFromFile
    void LoadFromMemory(char** cursor, float s, float t, float u) override; // 19: 005A2490 LoadFromMemoryImpl
    void LoadFig(char** cursor, float s, float t, float u) override;        // 20: 005A2500 LoadFigImpl
    void LoadOld(char** cursor, float s, float t, float u) override;        // 21: 005A3800 LoadOldImpl
    virtual void MirrorX();                                                 // 22: 005A4B30 MirrorXImpl
    virtual void MirrorY();                                                 // 23: 005A4BF0 MirrorYImpl

    // The registrations (C20 retires them): 00454510 the destructor, 004543A0 (slot 0) the
    // destructor, then operator delete when bit 0 of `flags` is set (returns this).
    void Destruct();
    CPlainFigureMesh* DeletingDestruct(unsigned flags);
};
static_assert(sizeof(CSharedFigureMesh) == 0x220 && sizeof(CPlainFigureMesh) == 0x220);

// The header in front of a FIG mesh: the magic ("FIG8" = 0x38474946 morphs) and the element
// counts, then the two dwords at +0x158 / +0x15C.
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

// 005AAE60. The figure loader's (005AC010) bounding volume of a FIG figure part: `data` holds the
// centre, box minimum, box maximum and radius, 8 versions each for FIG8 (morphed as in
// CFigureMesh::LoadBounds, except that the radius keeps its lower blend on the FPU) or one
// otherwise. Called with the loader in ECX, which it does not use. RET 0x18.
void __stdcall MorphFigureBounds(ClipBounds* out, const float* data, float s, float t, float u, std::uint32_t magic);
// 005AB9A0. The same for figure parts without a FIG header: 6 corner versions of each value,
// blended as the bones are. RET 0x14.
void __stdcall MorphFigureBounds6(ClipBounds* out, const float* data, float s, float t, float u);
