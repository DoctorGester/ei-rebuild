#pragma once

// The 3D library's figure meshes and their loading from .lnk link files (004F2650-004FBD80). A
// CLinkFigureLoader (model.h; one per world view, 00563710) loads a figure by name: its .lnk file
// lists the parts, each a FIG / FIG8 (or older headerless) record in figures.res / menus.res, morphed
// by three coefficients and appended to one CLinkMesh. See docs/notes/models.md, "Link figures".
// Names provisional.

#include "figure_mesh.h"   // AlignedArray, VertexPacket, NormalPacket, TexCoord, MeshCorner, FigHeader
#include "model.h"         // ClipBounds, TriangleIndexArray, CLinkFigureLoader

#include "lib/stlp.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

class CLight;        // spell.h

// The drawer of a figure mesh (CFigureMesh::drawer_; its draws 005A7A70 / 005A7640, 005A7160
// advances its animation, 005A7290 finishes a draw). Only what the draws use.
struct FigureMeshDrawer {
    std::uint8_t unknown_00[8];
    std::uint32_t flags_;       // +0x08 gets the bits of 00790C0C when the mesh is animated
    std::uint8_t unknown_0C[0x1C - 0x0C];
    CFigureMesh* mesh_;         // +0x1C the mesh it draws
    RECT screen_;               // +0x20 where the mesh was last drawn
};
static_assert(offsetof(FigureMeshDrawer, mesh_) == 0x1C && offsetof(FigureMeshDrawer, screen_) == 0x20);

struct CDXSurface;   // texture.h

// The transform object of 3DfpFPU.dll (GetTLPipeline; CTransformPipeline::transform_), as far as
// rewritten code calls it.
class ITransformPipeline {
public:
    virtual void Unknown0();
    // 1: the viewport's size in pixels (the renderer's creation, 00500B70).
    virtual void SetViewportSize(float width, float height);
    // 2: the sun for the next LightMesh (its localDirection_; 005A7A70).
    virtual void AddSun(CLight* sun);
    virtual void Unknown3();
    // 4: the projection matrix, transposed (00500B70 passes a copy).
    virtual void SetProjection(float* transposed);
    // 5: the depth range (00500B70: the options' fog start and far clip distances).
    virtual void SetDepthRange(float first, float second);
    virtual void Unknown6();
    // 7: fits `bounds` around `count` vertex packets.
    virtual void FitBounds(ClipBounds* bounds, const VertexPacket* vertices, int count);
    virtual void Unknown8();
    // 9: projects `blocks` blocks of four positions with `matrix` into `projected`, writing their
    // clip outcodes; the result is the caller's (on screen / crossing / outside).
    virtual int ProjectClipped(const Matrix4* matrix, const void* positions, void* projected, void* outcodes,
                               int blocks);
    virtual void Unknown10();
    virtual void Unknown11();
    virtual void Unknown12();
    virtual void Unknown13();
    // 14: the same without clipping (every block on screen).
    virtual void Project(const Matrix4* matrix, const void* positions, void* projected, int blocks);
    virtual void Unknown15();
    virtual void Unknown16();
    virtual void Unknown17();
    // 18 / 19: the same for the terrain's vertices (006BFC80).
    virtual int ProjectTerrainClipped(const Matrix4* matrix, const void* vertices, void* projected, void* outcodes,
                                      int count);
    virtual void ProjectTerrain(const Matrix4* matrix, const void* vertices, void* projected, int count);
    virtual void Unknown20();
    virtual void Unknown21();
    virtual void Unknown22();
    virtual void Unknown23();
    virtual void Unknown24();
    virtual void Unknown25();
    virtual void Unknown26();
    virtual void Unknown27();
    virtual void Unknown28();
    virtual void Unknown29();
    virtual void Unknown30();
    virtual void Unknown31();
    virtual void Unknown32();
    virtual void Unknown33();
    virtual void Unknown34();
    virtual void Unknown35();
    virtual void Unknown36();
    virtual void Unknown37();
    virtual void Unknown38();
    virtual void Unknown39();
    // 40: lights a figure mesh (005A7A70): its vertex packets and normals, the colours written (one
    // per normal), its words, the lights (their localPosition_, colours set for the material) and
    // the material's light vector.
    virtual void LightMesh(const void* vertices, const void* normals, std::uint32_t* colours, const void* words,
                           int wordCount, CLight** lights, int lightCount, const std::uint32_t* vector);
};

// A part's header in the mesh's map +0x14 (map<string, LinkPartHeader>; 0x50 bytes, zeroed when
// the map inserts it).
struct LinkPartHeader {
    std::uint32_t unknown_00;   // +0x00 the second dword after the FIG header
    std::uint32_t unknown_04;   // +0x04 the first one
    ClipBounds bounds;          // +0x08 fitted to the part's morphed vertices
    std::uint8_t unknown_34[0x0C];
    RECT screen;                // +0x40 where the part was last drawn (empty: not drawn)
};
static_assert(offsetof(LinkPartHeader, screen) == 0x40);
static_assert(offsetof(LinkPartHeader, bounds) == 0x08);
static_assert(sizeof(LinkPartHeader) == 0x50);

// Where a part's elements lie in the mesh's arrays (map +0x9C: map<string, LinkPartRanges>).
struct LinkPartRanges {
    struct Range {
        int start;
        int count;
    };
    Range vertices;   // +0x00
    Range normals;    // +0x08
    Range texCoords;  // +0x10
    Range corners;    // +0x18
    Range words;      // +0x20
    Range indices;    // +0x28
};
static_assert(sizeof(LinkPartRanges) == 0x30);

// The mesh's part maps (STLport map<string, T>; their insert / erase are library instantiations
// reached through their addresses, originals_script.h).
using LinkHeaderMap = stlp::map<stlp::string, LinkPartHeader>;   // nodes 0x6C bytes
using LinkRangeMap = stlp::map<stlp::string, LinkPartRanges>;    // nodes 0x4C bytes
static_assert(sizeof(LinkHeaderMap) == 0x0C && sizeof(LinkHeaderMap::Node) == 0x6C && sizeof(LinkRangeMap::Node) == 0x4C);

// A figure's mesh (vtable 0073D790; base 0073D7CC; constructor 004F2C00, destructor 004F2E40).
// The parts' elements are concatenated in the arrays; 0xB0 bytes. The game's constructor stores the
// vtable (novtable). Slots 7-14 (004F3870 ... 004F4850) draw parts through the device and are left to
// the renderer.
class __declspec(novtable) CLinkMesh {
public:
    // 0: 004F2E20 (RET 4, returns this). Destruct; freed when flags bit 0 is set.
    virtual void* DeleteThis(unsigned flags);
    // 1: 004F45E0. screenRect_ = the union of the parts' non-empty screen rectangles (inverted
    // +-2000000000 when there is none).
    virtual void UpdateScreenRect();
    // 2: 004F3040 (RET 4, AL). Whether `name` is a part.
    virtual bool HasPart(const stlp::string* name);
    // 3: 004F2FB0 (RET 4). Appends the part names, in map order, to `out`.
    virtual void CollectPartNames(stlp::vector<stlp::string>* out);
    // 4: 004F30D0 (RET 4, AL). The loader (004F7480) loads part `name` into this mesh.
    virtual bool LoadPart(const stlp::string* name);
    // 5: 004F3530 (RET 4, AL). Removes part `name`: false when there is none. The later elements of
    // each array move down over its elements; parts whose index range starts after the removed
    // one's get every start lowered by the removed part's starts (sic: not by its counts).
    virtual bool RemovePart(const stlp::string* name);
    // 6: 004F3780 (RET 0x18, AL). Part `name`'s first vertex packet, corner and index and its vertex
    // and index counts; false (nothing written) when there is no such part.
    virtual bool GetPartData(const stlp::string* name, VertexPacket** vertices, MeshCorner** corners,
                             std::uint16_t** indices, int* vertexCount, int* indexCount);
    // 7-12: a figure mesh's part `name` drawn through this figure (CObject3D::figure; 005A7A70):
    // 7 fills a flat part's colours and speculars, 8 lights it, 9-12 draw it (with the mesh's
    // per-vertex offsets: 9 / 11; with two textures' surfaces: 11 / 12). The draws return 2 when
    // nothing was drawn. `u`, `v` are the mesh's +0xCC / +0xD0.
    virtual void FlatColours(const stlp::string* name, std::uint32_t colour, std::uint32_t specular,
                             lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars);
    virtual void LightPart(const stlp::string* name, const FigureLights* lights, FigureMaterial* material, int alpha,
                           lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars);
    virtual int DrawPartMoved(const stlp::string* name, int flags, lib::AlignedArray<std::uint32_t>* colours,
                              lib::AlignedArray<std::uint32_t>* speculars, lib::AlignedArray<Vector3>* offsets,
                              std::uint32_t u, std::uint32_t v);
    virtual int DrawPart(const stlp::string* name, int flags, lib::AlignedArray<std::uint32_t>* colours,
                         lib::AlignedArray<std::uint32_t>* speculars, std::uint32_t u, std::uint32_t v);
    virtual int DrawPartMovedTextured(const stlp::string* name, int flags, CDXSurface* first, CDXSurface* second,
                                      lib::AlignedArray<std::uint32_t>* colours,
                                      lib::AlignedArray<std::uint32_t>* speculars,
                                      lib::AlignedArray<Vector3>* offsets, std::uint32_t u, std::uint32_t v);
    virtual int DrawPartTextured(const stlp::string* name, int flags, CDXSurface* first, CDXSurface* second,
                                 lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars,
                                 std::uint32_t u, std::uint32_t v);

    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073D790);
    static constexpr std::uintptr_t kBaseVtableAddress = std::uintptr_t(0x0073D7CC);   // the part-header base
    static constexpr std::uintptr_t kRootVtableAddress = std::uintptr_t(0x0073C368);   // the counted root (one slot)

    // 004F2C00 (RET 0x14, returns this). No references; the name copied, both maps and the arrays
    // empty, the coefficients stored, the pipeline taken from the loader's renderer (+0xAC -> +0xC).
    CLinkMesh* Construct(CLinkFigureLoader* loader, const stlp::string* name, float s, float t, float u);
    // 004F2E40. The ranges map, the arrays (indices first), then DestructBase's part.
    void Destruct();
    // 004F2D40 (the base's destructor; reached only from EH unwinding). The headers map and the
    // name; the root vtable left.
    void DestructBase();
    // 004F2E00 (RET 4, returns this): the base's deleting destructor (vtable 0073D7CC slot 0; no
    // object of the base alone exists).
    CLinkMesh* DeletingDestructBase(unsigned flags);

    // 004F4B50 (A-S8). An estimate of the mesh's bytes: 2 * (indices + 3 * corners + 2 * (words + 0x2C
    // + 2 * (texture coordinates + 8 * (vertices + normals) + 6 * parts))), from the element counts.
    int MemoryBytes() const;

    // 004F3800 (RET 0xC, AL; A-S8). Overwrites part `name`'s first min(its count, count) vertex
    // packets (unsigned) with `vertices`; false when there is no such part.
    bool SetPartVertices(const stlp::string* name, const VertexPacket* vertices, unsigned count);

    // 004F2F90 (RET 0xC). Sets the morph coefficients.
    void SetMorph(float s, float t, float u);
    // 004F7370 (RET 4, returns out; link_loader.cpp). Constructs a copy of the name in `out`.
    stlp::string* CopyName(stlp::string* out) const;

    // 004F30F0 (RET 0x34, AL). Appends a part: false when `name` is already a part; otherwise each
    // array grows by its count and gets the elements, and the part's ranges are recorded; true.
    bool AddPart(const stlp::string* name, const VertexPacket* vertices, int vertexCount, const NormalPacket* normals,
                int normalCount, const TexCoord* texCoords, int texCoordCount, const MeshCorner* corners,
                int cornerCount, const std::uint32_t* words, int wordCount, const std::uint16_t* indices,
                int indexCount);

    int refCount_;                    // +0x04
    stlp::string name_;               // +0x08
    LinkHeaderMap headers_;           // +0x14
    RECT screenRect_;                 // +0x20
    float morph_[3];                  // +0x30 the coefficients the figure was loaded with
    lib::AlignedArray<VertexPacket> vertices_;   // +0x3C
    lib::AlignedArray<NormalPacket> normals_;    // +0x4C
    lib::AlignedArray<TexCoord> texCoords_;      // +0x5C
    TriangleIndexArray corners_;            // +0x6C 6-byte triangle corners (MeshCorner)
    lib::AlignedArray<std::uint32_t> words_;     // +0x7C
    lib::AlignedArray<std::uint16_t> indices_;   // +0x8C
    LinkRangeMap ranges_;             // +0x9C
    CLinkFigureLoader* loader_;       // +0xA8
    ITransformPipeline* pipeline_;    // +0xAC
};
static_assert(offsetof(CLinkMesh, refCount_) == 0x04);
static_assert(offsetof(CLinkMesh, headers_) == 0x14);
static_assert(offsetof(CLinkMesh, screenRect_) == 0x20);
static_assert(offsetof(CLinkMesh, morph_) == 0x30);
static_assert(offsetof(CLinkMesh, vertices_) == 0x3C);
static_assert(offsetof(CLinkMesh, corners_) == 0x6C);
static_assert(offsetof(CLinkMesh, normals_) == 0x4C && offsetof(CLinkMesh, texCoords_) == 0x5C);
static_assert(offsetof(CLinkMesh, words_) == 0x7C && offsetof(CLinkMesh, indices_) == 0x8C);
static_assert(offsetof(CLinkMesh, loader_) == 0xA8);
// corners_ is indexed in whole triangles: one element is one 6-byte MeshCorner.
static_assert(sizeof(*CLinkMesh::corners_.data) == sizeof(MeshCorner) && sizeof(MeshCorner) == 6);
static_assert(sizeof(*CLinkMesh::words_.data) == 4 && sizeof(*CLinkMesh::indices_.data) == 2);
static_assert(offsetof(CLinkMesh, ranges_) == 0x9C);
static_assert(offsetof(CLinkMesh, pipeline_) == 0xAC);
static_assert(sizeof(CLinkMesh) == 0xB0);

// The record of a part in a resource archive (as the loaders read it; `cursor` moves past it):
// 004F7F00 the FIG / FIG8 record, 004F87D0 the older one without a header.
// Both are members of CLinkFigureLoader (model.h): LoadFigPart, LoadOldPart.

// link_loader.cpp (A-S7). 004DB9C0 (__fastcall: ECX out, EDX cursor): a length-prefixed string of a
// .lnk file (a four-byte length, then the characters) at *cursor; `out` gets the characters up to
// the first zero byte (copied through a static buffer, 007AA8D0) and keeps its old text for
// length 0. The cursor moves past the record.
void __fastcall ReadLinkString(stlp::string* out, const char** cursor);
