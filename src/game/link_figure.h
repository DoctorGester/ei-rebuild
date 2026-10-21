#pragma once

// The 3D library's figure meshes and their loading from .lnk link files (004F2650-004FBD80). A
// CLinkFigureLoader (model.h; one per world view, 00563710) loads a figure by name: its .lnk file
// lists the parts, each a FIG / FIG8 (or older headerless) record in figures.res / menus.res, morphed
// by three coefficients and appended to one CLinkMesh. See docs/notes/models.md, "Link figures".
// Names provisional.

#include "figure_mesh.h"   // AlignedArray, VertexPacket, NormalPacket, TexCoord, MeshCorner, FigHeader
#include "model.h"         // ClipBounds, TriangleIndexArray, CLinkFigureLoader
#include "texture.h"       // RasterTarget

#include "lib/stlp.h"

#include <windows.h>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

#include <cstddef>
#include <cstdint>

class CLight;        // spell.h

// The figure mesh drawer's base (game.exe's 0073FB44, retired; slots 2, 3, 11 and 12 pure): an
// engine object with the mesh it draws. No object of the base alone exists.
struct FigureDrawerBase : CEngineObject {
    // The deleting destructors (slot 0) free into the game's heap.
    static void operator delete(void* block) { gamecrt::operator_delete(block); }

    // The registrations (C20 retires them): 005A70C0 the destructor, 005A70A0 (slot 0 of the base,
    // RET 4) the destructor, then operator delete when bit 0 of `flags` is set (returns this).
    void DestructDrawerBase();
    FigureDrawerBase* DeletingDestructBase(unsigned flags);

    CFigureMesh* mesh_;         // +0x1C the mesh it draws

protected:
    // 005A7010's part: the engine object's fields (no references, id -1, no owner), no mesh.
    FigureDrawerBase();
    // 005A70C0's work: no mesh, then the engine object's.
    ~FigureDrawerBase() override;
};

// The drawer of a figure mesh (CFigureMesh::drawer_; its draws 005A7A70 / 005A7640, 005A7160
// advances its animation, 005A7290 finishes a draw). It is an engine object of class 0x36,
// "CFigureClientSpecific" (0x3C bytes): the client-specific manager's factory 0042D700 makes one
// for type 0x45 and CFigureManager::CreateFigureObject gives it to every new figure. A real virtual
// class (C19): the compiler makes its vtable (game.exe's 0073FB10 is retired). flags_ gets the bits
// of 00790C0C when the mesh is animated.
struct FigureMeshDrawer : FigureDrawerBase {
    // 0x005A7640 (figure_part_draw_plain.cpp).
    int DrawPlain(CTextureDevice* device, int, int, int, int alpha, RECT* rect);
    // 0x005A7A70 (figure_part_draw.cpp).
    int DrawLit(CTextureDevice* device, const FigureLights* lights, CMaterial* material, int alpha, RECT* rect,
                CTexture* textureA, CTexture* textureB);
    // 0x005A7290 (figure_part_bounds.cpp).
    void DrawBounds(CSceneRenderer* renderer);

    // figure_drawer.cpp (A-W14). 005A7010: FigureDrawerBase's constructor (the class adds nothing).
    FigureMeshDrawer();
    // 005A7080's work: no flags, then the base's destructor.
    ~FigureMeshDrawer() override;

    // The vtable: CEngineObject's but for 0, 2, 3, 11 and 12.
    int Create() override;                 // 2: 005A70E0 CreateDrawer
    void Destroy() override;               // 3: 005A7130 DestroyDrawer
    const char* GetClassName() override;   // 11: 005A7040 "CFigureClientSpecific" (world_class_slots.cpp)
    int GetClassId() override;             // 12: 005A7050 (0x36)

    // The registrations (C20 retires them): 005A7010 the constructor in place (returns this; the
    // client-specific factory, misc_400.cpp), 005A7080 the destructor, 005A7060 (slot 0, RET 4) the
    // destructor, then operator delete when bit 0 of `flags` is set (returns this).
    FigureMeshDrawer* Construct();
    void Destruct();
    FigureMeshDrawer* DeletingDestruct(unsigned flags);
    // 005A70E0 (slot 2). No flags; +0x30 becomes (1, 1, 1). Returns 1.
    int CreateDrawer();
    // 005A7130 (slot 3). No mesh, no flags.
    void DestroyDrawer();

    // 005A7160 (RET 4; figure_drawer.cpp; the draws call it through its address for the animated
    // mesh types 13..16). The texture offset of an animated mesh at `time` (truncated by _ftol):
    // type 13 cycles 8 frames over two rows (u = frame % 4 / 4, v = 0 or 0.5), 14 16 frames of a
    // 4 x 4 sheet, 16 32 frames of 8 x 4 (u = column / 4 or / 8, v = 1 - row / 4); 15 none.
    void Animate(float time);

    RECT screen_;               // +0x20 where the mesh was last drawn
    Vector3 spare30_;           // +0x30 (1, 1, 1) after Create; nothing reads it
};
static_assert(offsetof(FigureMeshDrawer, mesh_) == 0x1C && offsetof(FigureMeshDrawer, screen_) == 0x20);
static_assert(offsetof(FigureMeshDrawer, spare30_) == 0x30 && sizeof(FigureMeshDrawer) == 0x3C);

class CTextureDevice;   // texture.h
class CTexture;         // texture.h
// The drawer's draws (the renderer's slots 13 / 14 call them): 005A7A70 lit, with two textures
// (figure_part_draw.cpp), and 005A7640 plain (figure_part_draw_plain.cpp). Both return 1.

struct CDXSurface;   // texture.h

// A part's header in the mesh's map +0x14 (map<string, LinkPartHeader>; 0x50 bytes, zeroed when
// the map inserts it).
struct LinkPartHeader {
    // +0x00 / +0x04 the second / first dword after the FIG header: the part's CFigureMesh::meshType_
    // and materialValue_ (an object's part copies them and the box, CObject3D::LoadPart).
    std::uint32_t meshType;
    std::uint32_t materialValue;
    ClipBounds bounds;          // +0x08 fitted to the part's morphed vertices
    Vector3 pivot;              // +0x34 the part's position when loaded (CFigureMesh::boxPivot_)
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
    void* DeleteThisImpl(unsigned flags);   // body of the slot above (registered by member pointer)
    // 1: 004F45E0. screenRect_ = the union of the parts' non-empty screen rectangles (inverted
    // +-2000000000 when there is none).
    virtual void UpdateScreenRect();
    // 2: 004F3040 (RET 4, AL). Whether `name` is a part.
    virtual bool HasPart(const stlp::string* name);
    bool HasPartImpl(const stlp::string* name);   // body of the slot above (registered by member pointer)
    // 3: 004F2FB0 (RET 4). Appends the part names, in map order, to `out`.
    virtual void CollectPartNames(stlp::vector<stlp::string>* out);
    void CollectPartNamesImpl(stlp::vector<stlp::string>* out);   // body of the slot above (registered by member pointer)
    // 4: 004F30D0 (RET 4, AL). The loader (004F7480) loads part `name` into this mesh.
    virtual bool LoadPart(const stlp::string* name);
    bool LoadPartImpl(const stlp::string* name);   // body of the slot above (registered by member pointer)
    // 5: 004F3530 (RET 4, AL). Removes part `name`: false when there is none. The later elements of
    // each array move down over its elements; parts whose index range starts after the removed
    // one's get every start lowered by the removed part's starts (sic: not by its counts).
    virtual bool RemovePart(const stlp::string* name);
    bool RemovePartImpl(const stlp::string* name);   // body of the slot above (registered by member pointer)
    // 6: 004F3780 (RET 0x18, AL). Part `name`'s first vertex packet, corner and index and its vertex
    // and index counts; false (nothing written) when there is no such part.
    virtual bool GetPartData(const stlp::string* name, VertexPacket** vertices, MeshCorner** corners,
                             std::uint16_t** indices, int* vertexCount, int* indexCount);
    bool GetPartDataImpl(const stlp::string* name, VertexPacket** vertices, MeshCorner** corners,
                             std::uint16_t** indices, int* vertexCount, int* indexCount);   // body of the slot above (registered by member pointer)
    // 7-12 (link_mesh_draw.cpp, A-S12): a figure mesh's part `name` drawn through this figure
    // (CObject3D::figure; 005A7A70): 7 (004F3AD0) fills a flat part's colours and speculars, 8
    // (004F3870) lights it (both AL: false when there is no such part), 9-12 (004F3C60, 004F3FA0,
    // 004F42D0, 004F4150) draw it (with the mesh's per-vertex offsets: 9 / 11; with two textures'
    // surfaces: 11 / 12). The draws return 2 when nothing was drawn. `u`, `v` are the mesh's
    // +0xCC / +0xD0.
    virtual bool FlatColours(const stlp::string* name, std::uint32_t colour, std::uint32_t specular,
                             lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars);
    bool FlatColoursImpl(const stlp::string* name, std::uint32_t colour, std::uint32_t specular,
                             lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars);   // body of the slot above (registered by member pointer)
    virtual bool LightPart(const stlp::string* name, const FigureLights* lights, CMaterial* material, int alpha,
                           lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars);
    bool LightPartImpl(const stlp::string* name, const FigureLights* lights, CMaterial* material, int alpha,
                           lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars);   // body of the slot above (registered by member pointer)
    virtual int DrawPartMoved(const stlp::string* name, int flags, lib::AlignedArray<std::uint32_t>* colours,
                              lib::AlignedArray<std::uint32_t>* speculars, lib::AlignedArray<Vector3>* offsets,
                              std::uint32_t u, std::uint32_t v);
    int DrawPartMovedImpl(const stlp::string* name, int flags, lib::AlignedArray<std::uint32_t>* colours,
                              lib::AlignedArray<std::uint32_t>* speculars, lib::AlignedArray<Vector3>* offsets,
                              std::uint32_t u, std::uint32_t v);   // body of the slot above (registered by member pointer)
    virtual int DrawPart(const stlp::string* name, int flags, lib::AlignedArray<std::uint32_t>* colours,
                         lib::AlignedArray<std::uint32_t>* speculars, std::uint32_t u, std::uint32_t v);
    int DrawPartImpl(const stlp::string* name, int flags, lib::AlignedArray<std::uint32_t>* colours,
                         lib::AlignedArray<std::uint32_t>* speculars, std::uint32_t u, std::uint32_t v);   // body of the slot above (registered by member pointer)
    virtual int DrawPartMovedTextured(const stlp::string* name, int flags, CDXSurface* first, CDXSurface* second,
                                      lib::AlignedArray<std::uint32_t>* colours,
                                      lib::AlignedArray<std::uint32_t>* speculars,
                                      lib::AlignedArray<Vector3>* offsets, std::uint32_t u, std::uint32_t v);
    int DrawPartMovedTexturedImpl(const stlp::string* name, int flags, CDXSurface* first, CDXSurface* second,
                                      lib::AlignedArray<std::uint32_t>* colours,
                                      lib::AlignedArray<std::uint32_t>* speculars,
                                      lib::AlignedArray<Vector3>* offsets, std::uint32_t u, std::uint32_t v);   // body of the slot above (registered by member pointer)
    virtual int DrawPartTextured(const stlp::string* name, int flags, CDXSurface* first, CDXSurface* second,
                                 lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars,
                                 std::uint32_t u, std::uint32_t v);
    int DrawPartTexturedImpl(const stlp::string* name, int flags, CDXSurface* first, CDXSurface* second,
                                 lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars,
                                 std::uint32_t u, std::uint32_t v);   // body of the slot above (registered by member pointer)
    // 13 / 14 (004F4680, 004F4850): the shadow of part `name` (0051CE60): its points projected with
    // `matrix` (the part's per-vertex offsets added) into `points`, growing `bounds`; then its
    // projected points `pixels` rasterised into the shadow target. Both return the point count (0
    // when there is no such part).
    virtual int ProjectPart(const stlp::string* name, lib::AlignedArray<Vector3>* offsets, const float* matrix,
                            Vector2* points, float* bounds);
    int ProjectPartImpl(const stlp::string* name, lib::AlignedArray<Vector3>* offsets, const float* matrix,
                            Vector2* points, float* bounds);   // body of the slot above (registered by member pointer)
    virtual int RasterisePart(const stlp::string* name, const RasterTriangle::Point* pixels, RasterTarget* target);
    int RasterisePartImpl(const stlp::string* name, const RasterTriangle::Point* pixels, RasterTarget* target);   // body of the slot above (registered by member pointer)

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
    float morphWeights_[3];           // +0x30 the morph coefficients (s, t, u) the figure was loaded with
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
static_assert(offsetof(CLinkMesh, morphWeights_) == 0x30);
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
