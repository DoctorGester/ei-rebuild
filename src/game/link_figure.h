#pragma once

// The 3D library's figure meshes and their loading from .lnk link files. A CLinkFigureLoader (model.h; one
// per world view) loads a figure by name: its .lnk file lists the parts, each a FIG / FIG8 (or older
// headerless) record in figures.res / menus.res, morphed by three coefficients and appended to one CLinkMesh.
// See docs/notes/models.md, "Link figures". Names provisional.

#include "figure_mesh.h"   // VertexPacket, NormalPacket, TexCoord, MeshCorner, FigHeader
#include "model.h"         // ClipBounds, TriangleIndexArray, CLinkFigureLoader
#include "texture.h"       // RasterTarget

#include <map>
#include <vector>
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
    int DrawLit(CTextureDevice* device, const std::vector<CLight*>* lights, CMaterial* material, int alpha, RECT* rect,
                CTexture* textureA, CTexture* textureB);
    void DrawBounds(CSceneRenderer* renderer);

    FigureMeshDrawer();
    ~FigureMeshDrawer() override;

    // The vtable: CEngineObject's but for 0, 2, 3, 11 and 12.
    int Create() override;
    void Destroy() override;
    const char* GetClassName() override;   // "CFigureClientSpecific" (world_class_slots.cpp)
    int GetClassId() override;

    // No flags. Returns 1.
    int CreateDrawer();
    // No mesh, no flags.
    void DestroyDrawer();

    // figure_drawer.cpp; the draws call it through its address for the animated
    // mesh types 13..16). The texture offset of an animated mesh at `time` (truncated by _ftol):
    // type 13 cycles 8 frames over two rows (u = frame % 4 / 4, v = 0 or 0.5), 14 16 frames of a
    // 4 x 4 sheet, 16 32 frames of 8 x 4 (u = column / 4 or / 8, v = 1 - row / 4); 15 none.
    void Animate(float time);

    RECT screen_;               // where the mesh was last drawn
};

class CTextureDevice;

struct Surface;

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

// A figure's mesh's base (no object of the
// base alone exists): the reference count, the name and the part headers.
class CLinkMeshBase {
public:

    virtual ~CLinkMeshBase();
    // ScreenRect_ = the union of the parts' non-empty screen rectangles (inverted
    // +-2000000000 when there is none).
    virtual void UpdateScreenRect() = 0;
    // Whether `name` is a part.
    virtual bool HasPart(const std::string* name) = 0;
    // Appends the part names, in map order, to `out`.
    virtual void CollectPartNames(std::vector<std::string>* out) = 0;
    // The loader loads part `name` into this mesh.
    virtual bool LoadPart(const std::string* name) = 0;
    // Removes part `name`: false when there is none. The later elements of
    // each array move down over its elements; parts whose index range starts after the removed
    // one's get every start lowered by the removed part's starts (sic: not by its counts).
    virtual bool RemovePart(const std::string* name) = 0;
    // Part `name`'s first vertex packet, corner and index and its vertex
    // and index counts; false (nothing written) when there is no such part.
    virtual bool GetPartData(const std::string* name, VertexPacket** vertices, MeshCorner** corners,
                             std::uint16_t** indices, int* vertexCount, int* indexCount) = 0;
    // Draws (link_mesh_draw.cpp): a figure mesh's part `name` drawn through this figure
    // (CObject3D::figure): the first fills a flat part's colours and speculars, the second lights it
    // (both: false when there is no such part), the next four draw it (with the mesh's per-vertex
    // offsets: first / third; with two textures' surfaces: 11 / 12). The draws return 2 when nothing
    // was drawn. `u`, `v` are members of the mesh.
    virtual bool FlatColours(const std::string* name, std::uint32_t colour, std::uint32_t specular,
                             std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars) = 0;
    virtual bool LightPart(const std::string* name, const std::vector<CLight*>* lights, CMaterial* material, int alpha,
                           std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars) = 0;
    virtual int DrawPartMoved(const std::string* name, int flags, std::vector<std::uint32_t>* colours,
                              std::vector<std::uint32_t>* speculars, std::vector<Vector3>* offsets,
                              std::uint32_t u, std::uint32_t v) = 0;
    virtual int DrawPart(const std::string* name, int flags, std::vector<std::uint32_t>* colours,
                         std::vector<std::uint32_t>* speculars, std::uint32_t u, std::uint32_t v) = 0;
    virtual int DrawPartMovedTextured(const std::string* name, int flags, Surface* first, Surface* second,
                                      std::vector<std::uint32_t>* colours,
                                      std::vector<std::uint32_t>* speculars,
                                      std::vector<Vector3>* offsets, std::uint32_t u, std::uint32_t v) = 0;
    virtual int DrawPartTextured(const std::string* name, int flags, Surface* first, Surface* second,
                                 std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars,
                                 std::uint32_t u, std::uint32_t v) = 0;
    // The shadow of part `name`: its points projected with `matrix` (the part's per-vertex offsets added)
    // into `points`, growing `bounds`. Returns the point count (0 when there is no such part).
    virtual int ProjectPart(const std::string* name, std::vector<Vector3>* offsets, const float* matrix,
                            Vector2* points, float* bounds) = 0;

    int refCount_;
    std::string name_;
    std::map<std::string, LinkPartHeader> headers_;

protected:
    explicit CLinkMeshBase(const std::string* name);
    CLinkMeshBase(const CLinkMeshBase&) = delete;
    CLinkMeshBase& operator=(const CLinkMeshBase&) = delete;
};

// A figure's mesh: the parts' elements concatenated in the arrays. A real virtual class.
// The draws go through the device (link_mesh_draw.cpp).
class CLinkMesh : public CLinkMeshBase {
public:
    CLinkMesh(CLinkFigureLoader* loader, const std::string* name, float s, float t, float u);
    ~CLinkMesh() override;

    // Every slot of the base.
    void UpdateScreenRect() override;   // link_figure.cpp
    bool HasPart(const std::string* name) override;
    void CollectPartNames(std::vector<std::string>* out) override;
    bool LoadPart(const std::string* name) override;
    bool RemovePart(const std::string* name) override;
    bool GetPartData(const std::string* name, VertexPacket** vertices, MeshCorner** corners,
                     std::uint16_t** indices, int* vertexCount, int* indexCount) override;
    bool FlatColours(const std::string* name, std::uint32_t colour, std::uint32_t specular,
                     std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars) override;
    bool LightPart(const std::string* name, const std::vector<CLight*>* lights, CMaterial* material, int alpha,
                   std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars) override;
    int DrawPartMoved(const std::string* name, int flags, std::vector<std::uint32_t>* colours,
                      std::vector<std::uint32_t>* speculars, std::vector<Vector3>* offsets,
                      std::uint32_t u, std::uint32_t v) override;
    int DrawPart(const std::string* name, int flags, std::vector<std::uint32_t>* colours,
                 std::vector<std::uint32_t>* speculars, std::uint32_t u, std::uint32_t v) override;
    int DrawPartMovedTextured(const std::string* name, int flags, Surface* first, Surface* second,
                              std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars,
                              std::vector<Vector3>* offsets, std::uint32_t u, std::uint32_t v) override;
    int DrawPartTextured(const std::string* name, int flags, Surface* first, Surface* second,
                         std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars,
                         std::uint32_t u, std::uint32_t v) override;
    int ProjectPart(const std::string* name, std::vector<Vector3>* offsets, const float* matrix,
                    Vector2* points, float* bounds) override;

    // An estimate of the mesh's bytes: 2 * (indices + 3 * corners + 2 * (words + 0x2C
    // + 2 * (texture coordinates + 8 * (vertices + normals) + 6 * parts))), from the element counts.
    int MemoryBytes() const;

    // Overwrites part `name`'s first min(its count, count) vertex
    // packets (unsigned) with `vertices`; false when there is no such part.
    bool SetPartVertices(const std::string* name, const VertexPacket* vertices, unsigned count);

    // Sets the morph coefficients.
    void SetMorph(float s, float t, float u);

    // Appends a part: false when `name` is already a part; otherwise each array grows by its count and
    // gets the elements, and the part's ranges are recorded; true.
    bool AddPart(const std::string* name, const VertexPacket* vertices, int vertexCount, const NormalPacket* normals,
                int normalCount, const TexCoord* texCoords, int texCoordCount, const MeshCorner* corners,
                int cornerCount, const std::uint32_t* words, int wordCount, const std::uint16_t* indices,
                int indexCount);

    RECT screenRect_;
    float morphWeights_[3];           // the morph coefficients (s, t, u) the figure was loaded with
    std::vector<VertexPacket> vertices_;
    std::vector<NormalPacket> normals_;
    std::vector<TexCoord> texCoords_;
    std::vector<MeshCorner> corners_;            // 6-byte triangle corners (MeshCorner)
    std::vector<std::uint32_t> words_;
    std::vector<std::uint16_t> indices_;
    std::map<std::string, LinkPartRanges> ranges_;
    CLinkFigureLoader* loader_;
    ITransformPipeline* pipeline_;
};
// corners_ is indexed in whole triangles: one element is one 6-byte MeshCorner.
static_assert(sizeof(MeshCorner) == 6);

// A length-prefixed string of a .lnk file (a four-byte length, then the characters) at *cursor; `out` gets
// the characters up to the first zero byte (copied through a static buffer) and keeps its old text for length
// 0. The cursor moves past the record.
void ReadLinkString(std::string* out, const char** cursor);
