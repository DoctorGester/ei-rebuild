#pragma once

// The 3D library's figure meshes and their loading from .lnk link files. A LinkFigureLoader (model.h; one
// per world view) loads a figure by name: its .lnk file lists the parts, each a FIG / FIG8 (or older
// headerless) record in figures.res / menus.res, morphed by three coefficients and appended to one LinkMesh.
// See docs/notes/models.md, "Link figures".

#include "figure_mesh.h"   // VertexPacket, NormalPacket, TexCoord, MeshCorner, FigHeader
#include "model.h"         // ClipBounds, TriangleIndexArray, LinkFigureLoader
#include "texture.h"
#include "lib/rect.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The drawer of a figure mesh (FigureMesh::drawer). It is an engine object of class 0x36,
// "CFigureClientSpecific": the client-specific manager's factory makes one
// for type 0x45 and FigureManager::CreateObject gives it to every new figure. A real virtual
// class.
struct FigureMeshDrawer : EngineObject {
    FigureMesh* mesh = nullptr;   // the mesh it draws

    void DrawPlain(TextureDevice* device, Material*, int alpha, Rect* rect);
    void DrawLit(TextureDevice* device, const std::vector<Light*>& lights, Material* material, int alpha, Rect* rect,
                 Texture* textureA, Texture* textureB);
    void DrawBounds(SceneRenderer* renderer);

    FigureMeshDrawer();
    ~FigureMeshDrawer() override;

    void Create() override;
    void Destroy() override;   // no mesh, no flags
    const char* ClassName() override;   // "CFigureClientSpecific" (figure_part_draw.cpp)
    ClassId GetClassId() override;

    // The texture offset of an animated mesh (types 13..16, figure_part_draw.cpp) at `time` (truncated by _ftol):
    // type 13 cycles 8 frames over two rows (u = frame % 4 / 4, v = 0 or 0.5), 14 16 frames of a
    // 4 x 4 sheet, 16 32 frames of 8 x 4 (u = column / 4 or / 8, v = 1 - row / 4); 15 none.
    void Animate(float time);

    Rect screen;                // where the mesh was last drawn
};

class TextureDevice;

struct Surface;

// A part's header in the mesh's header map (map<string, LinkPartHeader>).
struct LinkPartHeader {
    // The second / first dword after the FIG header: the part's FigureMesh::meshType and
    // materialValue (an object's part copies them and the box, Object3D::LoadPart).
    std::uint32_t meshType = 0;
    std::uint32_t materialValue = 0;
    FootprintPart box;          // the bounds fitted to the part's morphed vertices, the part's position when loaded
    Rect screen;                // where the part was last drawn (empty: not drawn)
};

// Where a part's elements lie in the mesh's arrays (the ranges map: map<string, LinkPartRanges>).
struct LinkPartRanges {
    struct Range {
        int start = 0;
        int count = 0;
    };
    Range vertices;
    Range normals;
    Range texCoords;
    Range corners;
    Range words;
    Range indices;
};

// A figure's mesh: the parts' elements concatenated in the arrays, with the reference count, the name and the
// part headers. The draws go through the device (link_mesh_draw.cpp).
class LinkMesh {
public:
    LinkMesh(LinkFigureLoader* loader, std::string_view name, Morph morph);
    ~LinkMesh();
    LinkMesh(const LinkMesh&) = delete;
    LinkMesh& operator=(const LinkMesh&) = delete;

    void AddRef() { ++refCount; }
    // Drops a reference; the last one deletes the mesh.
    void Release() {
        if (--refCount == 0) delete this;
    }

    // Whether `name` is a part.
    bool HasPart(std::string_view name);
    // The part names, in map order.
    std::vector<std::string> PartNames();
    // Part `name`'s first vertex packet, corner and index and its vertex
    // and index counts; false (nothing written) when there is no such part.
    bool GetPartData(std::string_view name, VertexPacket** vertices, MeshCorner** corners,
                     std::uint16_t** indices, int* vertexCount, int* indexCount);
    // Draws (link_mesh_draw.cpp): a figure mesh's part `name` drawn through this figure
    // (Object3D::figure): the first fills a flat part's colours and speculars, the second lights it
    // (both: false when there is no such part), the next four draw it (with the mesh's per-vertex
    // offsets: first / third; with two textures' surfaces: 11 / 12). The draws return false when nothing
    // was drawn. `u`, `v` are members of the mesh.
    bool FlatColours(std::string_view name, std::uint32_t colour, std::uint32_t specular,
                     std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars);
    bool LightPart(std::string_view name, const std::vector<Light*>& lights, Material* material, int alpha,
                   std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars);
    bool DrawPartMoved(std::string_view name, DrawFlag flags, std::vector<std::uint32_t>* colours,
                      std::vector<std::uint32_t>* speculars, std::vector<Vector3>* offsets,
                      float u, float v);
    bool DrawPart(std::string_view name, DrawFlag flags, std::vector<std::uint32_t>* colours,
                 std::vector<std::uint32_t>* speculars, float u, float v);
    bool DrawPartMovedTextured(std::string_view name, DrawFlag flags, Surface* first, Surface* second,
                              std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars,
                              std::vector<Vector3>* offsets, float u, float v);
    bool DrawPartTextured(std::string_view name, DrawFlag flags, Surface* first, Surface* second,
                         std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars,
                         float u, float v);

    // Overwrites part `name`'s first min(its count, count) vertex
    // packets (unsigned) with `vertices`; false when there is no such part.
    bool SetPartVertices(std::string_view name, const VertexPacket* vertices, unsigned count);

    // Appends a part: false when `name` is already a part; otherwise each array grows by the elements given, and
    // the part's ranges are recorded; true.
    bool AddPart(const std::string& name, std::span<const VertexPacket> newVertices,
                 std::span<const NormalPacket> newNormals, std::span<const TexCoord> newTexCoords,
                 std::span<const MeshCorner> newCorners, std::span<const std::uint32_t> newWords,
                 std::span<const std::uint16_t> newIndices);

    int refCount = 0;
    std::string name;
    std::map<std::string, LinkPartHeader, std::less<>> headers;
    Morph morph;                      // the coefficients the figure was loaded (or last morphed) with
    std::vector<VertexPacket> vertices;
    std::vector<NormalPacket> normals;
    std::vector<TexCoord> texCoords;
    std::vector<MeshCorner> corners;             // 6-byte triangle corners (MeshCorner)
    std::vector<std::uint32_t> words;
    std::vector<std::uint16_t> indices;
    std::map<std::string, LinkPartRanges, std::less<>> ranges;
    LinkFigureLoader* loader = nullptr;
};

// A length-prefixed string of a .lnk file (a four-byte length, then the characters) at *cursor: the characters
// up to the first zero byte, or none for length 0 (the reader keeps its old text then). The cursor moves past
// the record.
std::optional<std::string> ReadLinkString(const char** cursor);
