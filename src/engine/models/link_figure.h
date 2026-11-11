#pragma once

// The 3D library's figure meshes and their loading from .lnk link files. A LinkFigureLoader (link_loader.h; one
// per world view) loads a figure by name: its .lnk file lists the parts, each a FIG / FIG8 (or older
// headerless) record in figures.res / menus.res, morphed by three coefficients and appended to one LinkMesh.
// See docs/notes/models.md, "Link figures".

#include "engine/models/figure_mesh.h"   // VertexPacket, NormalPacket, TexCoord, MeshCorner, FigHeader
#include "engine/models/link_loader.h"   // LinkFigureLoader, Morph
#include "engine/scene/pipeline.h"      // ClipBounds
#include "engine/models/texture.h"
#include "lib/rect.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace render {
struct MeshLighting;
}
class TextureDevice;

// The drawer of a figure mesh (FigureMesh::drawer). It is an engine object of class 0x36,
// "CFigureClientSpecific": the client-specific manager's factory makes one
// for type 0x45 and FigureManager::CreateObject gives it to every new figure. A real virtual
// class.
struct FigureMeshDrawer : EngineObject {
    FigureMesh* mesh = nullptr;   // the mesh it draws

    // An object's part (Object3DClientSpecific::DrawParts) drawn whole with `material`'s colours: placed by `model`
    // (its model space to the landscape's, or an interface figure's own) under `view` (the screen stack's top), lit
    // on the GPU by `lighting` (the object's; its model and material colours set here) unless the mesh is flat;
    // `alpha` the speculars' alpha byte. False when the owner's figure has no such part (nothing drawn).
    bool DrawLit(TextureDevice* device, const Matrix4& view, const Matrix4& model, render::MeshLighting& lighting,
                 Material* material, int alpha);
    // The sky dome (its lit colours, placed by `sky` under the screen stack's top) or, without it, an interface
    // sprite (placed by the screen stack's top).
    void DrawPlain(TextureDevice* device, const Matrix4* sky);
    // The part as a caster outside the view (render::CasterMesh, within render::BeginCaster): its arrays as DrawLit
    // hands them over, placed by `model`, with stage 0's `texture`.
    void HandOverCaster(const Matrix4& model, const Surface* texture);

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

    // Picking (WorldClient::Pick), from the mesh's last draw with a rectangle: the view depth (the screen stack's w)
    // where the ray from the eye through the centre of pixel `point` meets the nearest of the drawn triangles,
    // FLT_MAX when it meets none.
    float SurfaceDepth(const Point& point) const;

    Rect screen;                // where the mesh was last drawn: its box on the screen (a unit's or a lever's part)
    Matrix4 screenMatrix;       // the screen stack's top it was last drawn with (with screen): model to screen
};

class SceneRenderer;
class TextureDevice;

struct Surface;

// A part's header in the mesh's header map (map<string, LinkPartHeader>).
struct LinkPartHeader {
    // The second / first dword after the FIG header: the part's FigureMesh::meshType and
    // materialValue (an object's part copies them and the box, Object3D::LoadPart).
    std::uint32_t meshType = 0;
    std::uint32_t materialValue = 0;
    FootprintPart box;          // the bounds fitted to the part's morphed vertices, the part's position when loaded
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
    // Part `name`'s ranges in the arrays, null when there is no such part (a map node: it stays while the mesh lives).
    const LinkPartRanges* PartRanges(std::string_view name) const;
    // The part names, in map order.
    std::vector<std::string> PartNames();
    // Part `name`'s first vertex packet, corner and index and its vertex
    // and index counts; false (nothing written) when there is no such part.
    bool GetPartData(std::string_view name, VertexPacket** vertices, MeshCorner** corners,
                     std::uint16_t** indices, int* vertexCount, int* indexCount);

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
    std::uint64_t serial = 0;         // this mesh among every LinkMesh made (from 1): FigureMesh::figureRanges' key
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
