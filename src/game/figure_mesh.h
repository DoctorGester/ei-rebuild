#pragma once

// Figure meshes: the game-side figure part objects created by the figure factory 005AA8E0
// (class ids 0x41-0x44) and their loaders (0x005A0000-0x005B0000). Names provisional; see the
// "Figure meshes" section of docs/notes/world.md.

#include "maths.h"
#include "model.h"

#include "lib/containers.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

// Four vertex positions in SoA form for the transform library: x[4], y[4], z[4] and w[4] (the
// loaders set w to 1.0). Vertex i of a mesh is component i & 3 of packet i >> 2 (005A0CD0).
// 0x40 bytes.
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
class CFigureMesh;

struct FigureMeshDrawer;   // link_figure.h

// The material a figure part is drawn with (005A7A70's fourth argument): its flags, the colours the
// lights scale (CLight::SetLitColors, +0x20 / +0x40) and the vector the transform object lights with.
struct FigureMaterial {
    std::uint8_t unknown_00[8];
    std::uint8_t flags_;            // +0x08 0x10: the speculars are kept (draw flag 8)
    std::uint8_t unknown_09[0x50 - 0x09];
    std::uint32_t lightVector_[3];  // +0x50 (moved as dwords)
};
static_assert(offsetof(FigureMaterial, flags_) == 0x08 && offsetof(FigureMaterial, lightVector_) == 0x50);

class CLight;   // spell.h

// The lights a figure part is lit with ({data, count}; 005A7A70's third argument).
struct FigureLights {
    CLight** data;
    int count;
};

// The figure mesh of class 0x42 (0x220 bytes, vtable 0073C1C4); 0x43 (0073FC48) and 0x44 derive
// from it and share its loaders. Class 0x41 (0x1E0 bytes, vtable 0073FBF0) has the same layout up
// to +0x1E0 and its own loaders. No virtuals are declared: the vtable is a plain field.
class CFigureMesh {
public:
    // 005A18E0. Reads the bounding volume (centre, box, radius) that follows the mesh header and
    // morphs it: "FIG8" files hold 8 corner versions of each value, blended trilinearly by the
    // figure's three morph coefficients; other versions hold one. Sets the box and centre of
    // `bounds_`. RET 0x14.
    void LoadBounds(char** cursor, float s, float t, float u, std::uint32_t magic);

    // 005A2500 (vtable slot 20 of classes 0x42-0x44). Loads a "FIG" mesh from memory at *cursor
    // (advanced past it), morphing FIG8 vertices; once only (flag 0x20). RET 0x10.
    void LoadFig(char** cursor, float s, float t, float u);
    // 005A5C70 (slot 20 of class 0x41): the same, but only the vertices and records are copied;
    // the other buffers point into the file data.
    void LoadFigShared(char** cursor, float s, float t, float u);
    // 005A3800 (slot 21 of classes 0x42-0x44): the older format without a FIG header, chosen by
    // slot 19 (005A2490) when the data does not start with "FIG": 6 corner versions of the
    // bounds and of every vertex, each array preceded by its count. RET 0x10.
    void LoadOld(char** cursor, float s, float t, float u);
    // 005A4D60 (slot 21 of class 0x41): the same, the last four buffers pointing into the data.
    void LoadOldShared(char** cursor, float s, float t, float u);

    // 0059FDF0 (called by 00518100 for every part of a CObject3D): grows the world-space box
    // [min, max] by the 8 corners of this part's box (bounds_) transformed by its matrix
    // (00401570); nothing when the position is 1e4 or more from the origin. RET 0x18.
    void ExtendWorldBox(float* maxX, float* maxY, float* maxZ, float* minX, float* minY, float* minZ) const;

    // 005A4B30 (vtable slot 22) / 005A4BF0 (slot 23): mirror the mesh in x / y (negates that
    // component of every vertex and normal) and swap the first two corners of every triangle.
    void MirrorX();
    void MirrorY();
    // 005A6D40: points the buffer descriptor at the mesh's own arrays and sizes the colour arrays
    // (one per normal).
    void RefreshBuffers();
    // 005A0B10: copies `offsets` (any {Vector3* data; int count} pair) into offsets_. RET 4.
    struct OffsetSpan {
        const Vector3* data;
        int count;
    };
    void SetOffsets(const OffsetSpan* offsets);
    // 005A0CD0: draws through `pipelineOwner` (+0xC: the transform pipeline, slot 29) with the
    // vertices moved by offsets_ when there are any (into the shared buffer at 007C2E18). Returns
    // the vertex count (4 per packet). RET 0x10.
    int DrawOffset(void* pipelineOwner, void* arg1, void* arg2, void* arg3);
    // 0059F710 / 0059F8F0 (vtable slots, RET 4; figure_mesh_file.cpp): write the mesh to / read it
    // from an MFC CFile: the name (+0x120, length then characters), each array as a count and its
    // elements, a 0, then centre, box, radius and +0x158 / +0x15C.
    void SaveToFile(void* file);
    void LoadFromFile(void* file);
    // 0059F0F0 (slot 18, RET 4): copies another mesh's transform (00401490), name, the shared
    // buffer descriptors (texture coordinates, corners, words, indices), +0xDC and the bounds.
    void CopyFrom(const CFigureMesh* other);
    // 0059F510 (slot 15, RET 8): sets the rotation (slot 14 with `q` = w, x, y, z), rotates the
    // position (+0x1C) by it into +0x28 (+ `offset`, copied to +0x110) and rebuilds the rotation
    // matrix at +0xE0 from the stored quaternion (+0x44).
    void SetPose(const float* q, const float* offset);

    void* vtable_;                      // +0x000
    int refCount_;                      // +0x004 the engine object's reference count
    std::uint32_t flags_;               // +0x008 0x20: loaded
    std::uint8_t unknown_00C[0x1C - 0x0C];
    Vector3 position_;                  // +0x01C (CTransformObject: local position)
    std::uint8_t unknown_028[0x34 - 0x28];
    Quaternion rotation_;               // +0x034 (CTransformObject: local rotation; a bone places it, 0050C230)
    std::uint8_t unknown_044[0x94 - 0x44];
    FigureMeshDrawer* drawer_;          // +0x094 (00501D00 draws through it)
    CObject3D* owner_;                  // +0x098 the object whose figure (+0xF0) may draw the mesh
    MeshBuffers buffers_;               // +0x09C
    std::uint32_t unknown_0CC;          // +0x0CC passed to the draws after the buffers, with +0xD0
    std::uint32_t unknown_0D0;          // +0x0D0
    std::uint32_t flatColour_;          // +0x0D4 every colour of a flat mesh (flags_ 0x100)
    std::uint32_t flatSpecular_;        // +0x0D8 every specular of a flat mesh
    std::uint8_t unknown_0DC[0x120 - 0xDC];
    mfc::CString name_;                 // +0x120 (the scene loader names it after its mesh)
    mfc::CString directory_;            // +0x124 where its .fig file is (005AC2F0; NewFigure empties it)
    lib::AlignedArray<Vector3> offsets_;     // +0x128 per-vertex offsets added when drawing (005A0B10 / 005A0CD0)
    lib::AlignedArray<std::uint32_t> colorsA_;   // +0x138 one per normal (lit colours)
    lib::AlignedArray<std::uint32_t> colorsB_;   // +0x148 one per normal
    std::uint32_t unknown_158;          // +0x158 from the file header
    std::uint32_t unknown_15C;          // +0x15C from the file header; the mesh type (13..16: animated, 005A7A70)
    Vector3 center_;                    // +0x160
    Vector3 boxMin_;                    // +0x16C
    Vector3 boxMax_;                    // +0x178
    float radius_;                      // +0x184
    ClipBounds bounds_;                 // +0x188
    std::uint8_t unknown_1B4[0x1C0 - 0x1B4];
    lib::AlignedArray<VertexPacket> vertices_;     // +0x1C0
    lib::AlignedArray<NormalPacket> normals_;    // +0x1D0
    // Class 0x41 ends here (0x1E0 bytes).
    lib::AlignedArray<MeshCorner> corners_;  // +0x1E0
    lib::AlignedArray<std::uint32_t> words_;     // +0x1F0
    lib::AlignedArray<std::uint16_t> indices_;    // +0x200
    lib::AlignedArray<TexCoord> texCoords_;       // +0x210
};
static_assert(offsetof(CFigureMesh, flags_) == 0x08 && offsetof(CFigureMesh, rotation_) == 0x34);
static_assert(offsetof(CFigureMesh, buffers_) == 0x9C);
static_assert(offsetof(CFigureMesh, drawer_) == 0x94 && offsetof(CFigureMesh, owner_) == 0x98);
static_assert(offsetof(CFigureMesh, unknown_0CC) == 0xCC && offsetof(CFigureMesh, flatSpecular_) == 0xD8);
static_assert(offsetof(CFigureMesh, name_) == 0x120);
static_assert(offsetof(CFigureMesh, colorsA_) == 0x138);
static_assert(offsetof(CFigureMesh, unknown_158) == 0x158);
static_assert(offsetof(CFigureMesh, center_) == 0x160);
static_assert(offsetof(CFigureMesh, radius_) == 0x184);
static_assert(offsetof(CFigureMesh, bounds_) == 0x188);
static_assert(offsetof(CFigureMesh, vertices_) == 0x1C0);
static_assert(offsetof(CFigureMesh, corners_) == 0x1E0);
static_assert(offsetof(CFigureMesh, texCoords_) == 0x210);
static_assert(sizeof(CFigureMesh) == 0x220);

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
    std::uint32_t unknown_1C;
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
