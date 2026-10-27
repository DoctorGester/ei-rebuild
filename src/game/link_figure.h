#pragma once

// The 3D library's figure meshes and their loading from .lnk link files. A CLinkFigureLoader (model.h; one
// per world view) loads a figure by name: its .lnk file lists the parts, each a FIG / FIG8 (or older
// headerless) record in figures.res / menus.res, morphed by three coefficients and appended to one CLinkMesh.
// See docs/notes/models.md, "Link figures". Names provisional.

#include "figure_mesh.h"   // AlignedArray, VertexPacket, NormalPacket, TexCoord, MeshCorner, FigHeader
#include "model.h"         // ClipBounds, TriangleIndexArray, CLinkFigureLoader
#include "texture.h"       // RasterTarget

#include "lib/stlp.h"

#include <windows.h>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

#include <cstddef>
#include <cstdint>

// The figure mesh drawer's base (an engine object with the mesh it draws).
// No object of the base alone exists.
struct FigureDrawerBase : CEngineObject {

    CFigureMesh* mesh_;         // the mesh it draws

protected:
    FigureDrawerBase();
    ~FigureDrawerBase() override;
};

// The drawer of a figure mesh (CFigureMesh::drawer_). It is an engine object of class 0x36,
// "CFigureClientSpecific": the client-specific manager's factory makes one
// for type 0x45 and CFigureManager::CreateFigureObject gives it to every new figure. A real virtual
// class. flags_ gets the bits
// of the animated-mesh flag word when the mesh is animated.
struct FigureMeshDrawer : FigureDrawerBase {
    int DrawPlain(CTextureDevice* device, const void*, const void*, CMaterial*, int alpha, RECT* rect);
    int DrawLit(CTextureDevice* device, const FigureLights* lights, CMaterial* material, int alpha, RECT* rect,
                CTexture* textureA, CTexture* textureB);
    void DrawBounds(CSceneRenderer* renderer);

    FigureMeshDrawer();
    ~FigureMeshDrawer() override;

    // The vtable: CEngineObject's but for 0, 2, 3, 11 and 12.
    int Create() override;
    void Destroy() override;
    const char* GetClassName() override;   // "CFigureClientSpecific" (world_class_slots.cpp)
    int GetClassId() override;

    // No flags; spare30_ becomes (1, 1, 1). Returns 1.
    int CreateDrawer();
    // No mesh, no flags.
    void DestroyDrawer();

    // figure_drawer.cpp; the draws call it through its address for the animated
    // mesh types 13..16). The texture offset of an animated mesh at `time` (truncated by _ftol):
    // type 13 cycles 8 frames over two rows (u = frame % 4 / 4, v = 0 or 0.5), 14 16 frames of a
    // 4 x 4 sheet, 16 32 frames of 8 x 4 (u = column / 4 or / 8, v = 1 - row / 4); 15 none.
    void Animate(float time);

    RECT screen_;               // where the mesh was last drawn
    Vector3 spare30_;           // (1, 1, 1) after Create; nothing reads it
};
static_assert(offsetof(FigureMeshDrawer, mesh_) == 0x1C && offsetof(FigureMeshDrawer, screen_) == 0x20);
static_assert(offsetof(FigureMeshDrawer, spare30_) == 0x30 && sizeof(FigureMeshDrawer) == 0x3C);

class CTextureDevice;

struct CDXSurface;

// A part's header in the mesh's header map (map<string, LinkPartHeader>; 0x50 bytes, zeroed when the
// map inserts it).
struct LinkPartHeader {
    // The second / first dword after the FIG header: the part's CFigureMesh::meshType_ and
    // materialValue_ (an object's part copies them and the box, CObject3D::LoadPart).
    std::uint32_t meshType;
    std::uint32_t materialValue;
    FootprintPart box;          // the bounds fitted to the part's morphed vertices, the part's position when loaded
    RECT screen;                // where the part was last drawn (empty: not drawn)
};
static_assert(offsetof(LinkPartHeader, screen) == 0x40);
static_assert(offsetof(LinkPartHeader, box) == 0x08);
static_assert(sizeof(LinkPartHeader) == 0x50);

// Where a part's elements lie in the mesh's arrays (the ranges map: map<string, LinkPartRanges>).
struct LinkPartRanges {
    struct Range {
        int start;
        int count;
    };
    Range vertices;
    Range normals;
    Range texCoords;
    Range corners;
    Range words;
    Range indices;
};
static_assert(sizeof(LinkPartRanges) == 0x30);

// The mesh's part maps (STLport map<string, T>; their insert / erase are library instantiations).
using LinkHeaderMap = stlp::map<stlp::string, LinkPartHeader>;   // nodes 0x6C bytes
using LinkRangeMap = stlp::map<stlp::string, LinkPartRanges>;    // nodes 0x4C bytes
static_assert(sizeof(LinkHeaderMap) == 0x0C && sizeof(LinkHeaderMap::Node) == 0x6C && sizeof(LinkRangeMap::Node) == 0x4C);

// A figure's mesh's base (no object of the
// base alone exists): the reference count, the name and the part headers.
class CLinkMeshBase {
public:

    virtual ~CLinkMeshBase();
    // ScreenRect_ = the union of the parts' non-empty screen rectangles (inverted
    // +-2000000000 when there is none).
    virtual void UpdateScreenRect() = 0;
    // Whether `name` is a part.
    virtual bool HasPart(const stlp::string* name) = 0;
    // Appends the part names, in map order, to `out`.
    virtual void CollectPartNames(stlp::vector<stlp::string>* out) = 0;
    // The loader loads part `name` into this mesh.
    virtual bool LoadPart(const stlp::string* name) = 0;
    // Removes part `name`: false when there is none. The later elements of
    // each array move down over its elements; parts whose index range starts after the removed
    // one's get every start lowered by the removed part's starts (sic: not by its counts).
    virtual bool RemovePart(const stlp::string* name) = 0;
    // Part `name`'s first vertex packet, corner and index and its vertex
    // and index counts; false (nothing written) when there is no such part.
    virtual bool GetPartData(const stlp::string* name, VertexPacket** vertices, MeshCorner** corners,
                             std::uint16_t** indices, int* vertexCount, int* indexCount) = 0;
    // Draws (link_mesh_draw.cpp): a figure mesh's part `name` drawn through this figure
    // (CObject3D::figure): the first fills a flat part's colours and speculars, the second lights it
    // (both: false when there is no such part), the next four draw it (with the mesh's per-vertex
    // offsets: first / third; with two textures' surfaces: 11 / 12). The draws return 2 when nothing
    // was drawn. `u`, `v` are members of the mesh.
    virtual bool FlatColours(const stlp::string* name, std::uint32_t colour, std::uint32_t specular,
                             lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars) = 0;
    virtual bool LightPart(const stlp::string* name, const FigureLights* lights, CMaterial* material, int alpha,
                           lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars) = 0;
    virtual int DrawPartMoved(const stlp::string* name, int flags, lib::AlignedArray<std::uint32_t>* colours,
                              lib::AlignedArray<std::uint32_t>* speculars, lib::AlignedArray<Vector3>* offsets,
                              std::uint32_t u, std::uint32_t v) = 0;
    virtual int DrawPart(const stlp::string* name, int flags, lib::AlignedArray<std::uint32_t>* colours,
                         lib::AlignedArray<std::uint32_t>* speculars, std::uint32_t u, std::uint32_t v) = 0;
    virtual int DrawPartMovedTextured(const stlp::string* name, int flags, CDXSurface* first, CDXSurface* second,
                                      lib::AlignedArray<std::uint32_t>* colours,
                                      lib::AlignedArray<std::uint32_t>* speculars,
                                      lib::AlignedArray<Vector3>* offsets, std::uint32_t u, std::uint32_t v) = 0;
    virtual int DrawPartTextured(const stlp::string* name, int flags, CDXSurface* first, CDXSurface* second,
                                 lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars,
                                 std::uint32_t u, std::uint32_t v) = 0;
    // The shadow of part `name`: its points projected with `matrix` (the part's per-vertex offsets added)
    // into `points`, growing `bounds`; then its projected points `pixels` rasterised into the shadow target.
    // Both return the point count (0 when there is no such part).
    virtual int ProjectPart(const stlp::string* name, lib::AlignedArray<Vector3>* offsets, const float* matrix,
                            Vector2* points, float* bounds) = 0;
    virtual int RasterisePart(const stlp::string* name, const RasterTriangle::Point* pixels, RasterTarget* target) = 0;

    static void operator delete(void* block) { gamecrt::operator_delete(block); }

    int refCount_;
    stlp::string name_;
    LinkHeaderMap headers_;

protected:
    explicit CLinkMeshBase(const stlp::string* name);
};

// A figure's mesh: the parts' elements concatenated in the arrays. A real virtual class.
// The draws go through the device (link_mesh_draw.cpp).
class CLinkMesh : public CLinkMeshBase {
public:
    CLinkMesh(CLinkFigureLoader* loader, const stlp::string* name, float s, float t, float u);
    ~CLinkMesh() override;

    // Every slot of the base.
    void UpdateScreenRect() override;   // link_figure.cpp
    bool HasPart(const stlp::string* name) override;
    void CollectPartNames(stlp::vector<stlp::string>* out) override;
    bool LoadPart(const stlp::string* name) override;
    bool RemovePart(const stlp::string* name) override;
    bool GetPartData(const stlp::string* name, VertexPacket** vertices, MeshCorner** corners,
                     std::uint16_t** indices, int* vertexCount, int* indexCount) override;
    bool FlatColours(const stlp::string* name, std::uint32_t colour, std::uint32_t specular,
                     lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars) override;
    bool LightPart(const stlp::string* name, const FigureLights* lights, CMaterial* material, int alpha,
                   lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars) override;
    int DrawPartMoved(const stlp::string* name, int flags, lib::AlignedArray<std::uint32_t>* colours,
                      lib::AlignedArray<std::uint32_t>* speculars, lib::AlignedArray<Vector3>* offsets,
                      std::uint32_t u, std::uint32_t v) override;
    int DrawPart(const stlp::string* name, int flags, lib::AlignedArray<std::uint32_t>* colours,
                 lib::AlignedArray<std::uint32_t>* speculars, std::uint32_t u, std::uint32_t v) override;
    int DrawPartMovedTextured(const stlp::string* name, int flags, CDXSurface* first, CDXSurface* second,
                              lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars,
                              lib::AlignedArray<Vector3>* offsets, std::uint32_t u, std::uint32_t v) override;
    int DrawPartTextured(const stlp::string* name, int flags, CDXSurface* first, CDXSurface* second,
                         lib::AlignedArray<std::uint32_t>* colours, lib::AlignedArray<std::uint32_t>* speculars,
                         std::uint32_t u, std::uint32_t v) override;
    int ProjectPart(const stlp::string* name, lib::AlignedArray<Vector3>* offsets, const float* matrix,
                    Vector2* points, float* bounds) override;
    int RasterisePart(const stlp::string* name, const RasterTriangle::Point* pixels, RasterTarget* target) override;

    // An estimate of the mesh's bytes: 2 * (indices + 3 * corners + 2 * (words + 0x2C
    // + 2 * (texture coordinates + 8 * (vertices + normals) + 6 * parts))), from the element counts.
    int MemoryBytes() const;

    // Overwrites part `name`'s first min(its count, count) vertex
    // packets (unsigned) with `vertices`; false when there is no such part.
    bool SetPartVertices(const stlp::string* name, const VertexPacket* vertices, unsigned count);

    // Sets the morph coefficients.
    void SetMorph(float s, float t, float u);
    // Constructs a copy of the name in `out` (link_loader.cpp).
    stlp::string* CopyName(stlp::string* out) const;

    // Appends a part: false when `name` is already a part; otherwise each array grows by its count and
    // gets the elements, and the part's ranges are recorded; true.
    bool AddPart(const stlp::string* name, const VertexPacket* vertices, int vertexCount, const NormalPacket* normals,
                int normalCount, const TexCoord* texCoords, int texCoordCount, const MeshCorner* corners,
                int cornerCount, const std::uint32_t* words, int wordCount, const std::uint16_t* indices,
                int indexCount);

    RECT screenRect_;
    float morphWeights_[3];           // the morph coefficients (s, t, u) the figure was loaded with
    lib::AlignedArray<VertexPacket> vertices_;
    lib::AlignedArray<NormalPacket> normals_;
    lib::AlignedArray<TexCoord> texCoords_;
    TriangleIndexArray corners_;            // 6-byte triangle corners (MeshCorner)
    lib::AlignedArray<std::uint32_t> words_;
    lib::AlignedArray<std::uint16_t> indices_;
    LinkRangeMap ranges_;
    CLinkFigureLoader* loader_;
    ITransformPipeline* pipeline_;
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

// A length-prefixed string of a .lnk file (a four-byte length, then the characters) at *cursor; `out` gets
// the characters up to the first zero byte (copied through a static buffer) and keeps its old text for length
// 0. The cursor moves past the record.
void __fastcall ReadLinkString(stlp::string* out, const char** cursor);
