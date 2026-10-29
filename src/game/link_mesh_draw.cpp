// LinkMesh's draw slots (link_figure.h): a part of a figure
// coloured, lit, drawn through the device's mesh draws and, for its shadow, projected and rasterised. The
// callers are the figure part draw (figure_part_draw.cpp) and the shadow
// (map_object_shadow.cpp), through the vtable only. The old Direct3D 7 path: the two plain draws
// turn the device's specular on around the draw for a material that keeps its speculars.
#include "core_maths.h"
#include "link_figure.h"
#include "material.h"        // Material
#include "render/render.h"   // MeshChanged
#include "render/render_state.h"
#include "texture.h"   // TextureDevice

#include "lib/crt.h"

#include "ei/x87.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

constexpr int kDrawScreenRect = 1;    // draw flag: the part's screen rectangle is kept
constexpr int kDrawSpecular = 8;      // draw flag: the material keeps its speculars
constexpr std::uint8_t kMaterialKeepsSpecular = 0x10;
constexpr int kOpaque = 0xFF;         // the alpha the draws pass (read with draw flag 4 only)
constexpr int kNothingDrawn = 2;      // the device's result, also for a part the figure does not have
constexpr float kByteScale = 255.0f;

// The positions of a part moved by its per-vertex offsets (one work array for the draws and the shadow's
// projection).
std::vector<VertexPacket> g_movedPackets;

// The screen rectangle of a part that was not drawn.
constexpr RECT kNoScreen = /*0073D780*/ {0, 0, 0, 0};

void Fill(std::vector<std::uint32_t>& array, std::uint32_t value) {
    for (int i = 0; i < std::ssize(array); ++i) array[i] = value;
}

// Vertex i of `packets` (component i & 3 of packet i >> 2), coordinate `axis`.
float& Coordinate(VertexPacket* packets, int i, int axis) { return packets[i >> 2].block[axis][i & 3]; }

// `source` (`packets` packets) with the offsets added, in the work array: a vertex beyond the
// offsets is copied; the w row is not written.
const VertexPacket* MovedPositions(const VertexPacket* source, int packets, const std::vector<Vector3>* offsets) {
    auto& moved = g_movedPackets;
    moved.resize(packets);
    auto* from = const_cast<VertexPacket*>(source);
    for (int i = 0; i < std::ssize(moved) * 4; ++i) {
        if (i < std::ssize(*offsets)) {
            const Vector3& offset = (*offsets)[i];
            Coordinate(moved.data(), i, 0) = offset.x + Coordinate(from, i, 0);
            Coordinate(moved.data(), i, 1) = offset.y + Coordinate(from, i, 1);
            Coordinate(moved.data(), i, 2) = offset.z + Coordinate(from, i, 2);
        } else {
            for (int axis = 0; axis < 3; ++axis)
                Coordinate(moved.data(), i, axis) = Coordinate(from, i, axis);
        }
    }
    return moved.data();
}

// What a draw needs of the mesh: a part's ranges, resolved to the device's mesh draw arguments.
struct PartDraw {
    LinkMesh* mesh;
    const LinkPartRanges& r;
    const VertexPacket* positions;
    std::vector<std::uint32_t>* colours;
    std::vector<std::uint32_t>* speculars;
    std::uint32_t u;
    std::uint32_t v;
    int flags;

    // TextureDevice::DrawMeshBlocks / 2: positions and
    // packets, normals and count, colours, speculars, texture coordinates and count, corners and
    // count, words and count, indices and count, the texture offsets, alpha, flags.
    int Draw(TextureDevice* device, Surface* first, Surface* second, bool textured) const {
        auto* indices = reinterpret_cast<std::uint16_t*>(mesh->indices.data() + r.indices.start);
        const auto* words = mesh->words.data() + r.words.start;
        const auto* corners = mesh->corners.data() + r.corners.start;
        const auto* texCoords = mesh->texCoords.data() + r.texCoords.start;
        const auto* normals = mesh->normals.data() + r.normals.start;
        const float uOffset = std::bit_cast<float>(u);
        const float vOffset = std::bit_cast<float>(v);
        if (textured) {
            return device->DrawMeshBlocks2(first, second, positions, r.vertices.count, normals, r.normals.count,
                                           colours->data(), speculars->data(), texCoords, r.texCoords.count, corners,
                                           r.corners.count, words, r.words.count, indices, r.indices.count, uOffset,
                                           vOffset, kOpaque, flags);
        }
        return device->DrawMeshBlocks(positions, r.vertices.count, normals, r.normals.count, colours->data(),
                                      speculars->data(), texCoords, r.texCoords.count, corners, r.corners.count, words,
                                      r.words.count, indices, r.indices.count, uOffset, vOffset, kOpaque, flags);
    }
};

// After a draw: the part's header takes the rectangle the device drew (draw flag 1, something
// drawn) or the empty one.
void KeepScreen(LinkMesh* mesh, const std::string* name, int flags, int result) {
    if ((flags & kDrawScreenRect) && result != kNothingDrawn) {
        const RECT& drawn = mesh->loader->renderer->drawnRect;
        mesh->headers[*name].screen = drawn;
        return;
    }
    mesh->headers[*name].screen = kNoScreen;
}

// The four draws: `offsets` (null: none asked) moves the positions when it has any; without
// surfaces the device's specular is on around the draw for draw flag 8.
int DrawPartCommon(LinkMesh* mesh, const std::string* name, int flags, Surface* first, Surface* second,
                   bool textured, std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars,
                   const std::vector<Vector3>* offsets, std::uint32_t u, std::uint32_t v) {
    auto node = mesh->ranges.find(*name);
    if (node == mesh->ranges.end()) return kNothingDrawn;
    const LinkPartRanges& r = node->second;
    const VertexPacket* positions = mesh->vertices.data() + r.vertices.start;
    if (offsets && !offsets->empty()) positions = MovedPositions(positions, r.vertices.count, offsets);

    const bool specular = !textured && (flags & kDrawSpecular) != 0;
    if (specular) render::g_renderState.specular = true;
    const PartDraw draw{mesh, r, positions, colours, speculars, u, v, flags};
    const int result = draw.Draw(mesh->loader->renderer, first, second, textured);
    if (specular) render::g_renderState.specular = false;
    KeepScreen(mesh, name, flags, result);
    return result;
}

} // namespace

// ---- 7 / 8: the colours ----------------------------------------------------------------------------

// Both arrays sized to four per normal packet (their contents dropped), then filled.
bool LinkMesh::FlatColours(const std::string* partName, std::uint32_t colour, std::uint32_t specular,
                            std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars) {
    auto node = ranges.find(*partName);
    if (node == ranges.end()) return false;
    colours->resize(node->second.normals.count * 4);
    speculars->resize(node->second.normals.count * 4);
    Fill(*colours, colour);
    Fill(*speculars, specular);
    render::MeshChanged(colours->data(), render::MeshChange::Colours);   // the colours the new renderer keeps
    return true;
}

// The arrays sized as above; the colours the part lit on the GPU keeps
// (Material::GpuLitColour; the transform object's LightMesh lit them before); every specular is the alpha
// byte in the top byte, over the material's specular colour (components times 255, truncated by
// _ftol and OR-ed in unmasked) when the material keeps its speculars.
bool LinkMesh::LightPart(const std::string* partName, const std::vector<CLight*>* lights, Material* material, int alpha,
                          std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars) {
    auto node = ranges.find(*partName);
    if (node == ranges.end()) return false;
    const LinkPartRanges& r = node->second;
    colours->resize(r.normals.count * 4);
    speculars->resize(r.normals.count * 4);

    (void)lights;   // the GPU's (figure_part_draw.cpp, PartLighting)
    Fill(*colours, material->GpuLitColour());

    const std::uint32_t alphaByte = static_cast<std::uint32_t>(alpha) & 0xFF;
    if (material->flags & kMaterialKeepsSpecular) {
        std::uint32_t packed = libcrt::Ftol(material->colors.specular.x * kByteScale) | (alphaByte << 8);
        packed <<= 8;
        packed |= libcrt::Ftol(material->colors.specular.y * kByteScale);
        packed <<= 8;
        packed |= libcrt::Ftol(material->colors.specular.z * kByteScale);
        Fill(*speculars, packed);
    } else {
        Fill(*speculars, alphaByte << 24);
    }
    render::MeshChanged(colours->data(), render::MeshChange::Colours);   // the colours the new renderer keeps
    return true;
}

// ---- 9-12: the draws -------------------------------------------------------------------------------

int LinkMesh::DrawPartMoved(const std::string* partName, int flags, std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars,
                             std::vector<Vector3>* offsets, std::uint32_t u, std::uint32_t v) {
    return DrawPartCommon(this, partName, flags, nullptr, nullptr, false, colours, speculars, offsets, u, v);
}

int LinkMesh::DrawPart(const std::string* partName, int flags, std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars,
                        std::uint32_t u, std::uint32_t v) {
    return DrawPartCommon(this, partName, flags, nullptr, nullptr, false, colours, speculars, nullptr, u, v);
}

int LinkMesh::DrawPartMovedTextured(const std::string* partName, int flags, Surface* first, Surface* second,
                                     std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars,
                                     std::vector<Vector3>* offsets, std::uint32_t u, std::uint32_t v) {
    return DrawPartCommon(this, partName, flags, first, second, true, colours, speculars, offsets, u, v);
}

int LinkMesh::DrawPartTextured(const std::string* partName, int flags, Surface* first, Surface* second,
                                std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars, std::uint32_t u, std::uint32_t v) {
    return DrawPartCommon(this, partName, flags, first, second, true, colours, speculars, nullptr, u, v);
}

// ---- 13 / 14: the shadow ---------------------------------------------------------------------------

// The part's positions (moved by `offsets` when it has any) through
// the flat projection (ProjectFlat); four points per packet.
int LinkMesh::ProjectPart(const std::string* partName, std::vector<Vector3>* offsets, const Matrix4& matrix,
                           Vector2* points, float* bounds) {
    auto node = ranges.find(*partName);
    if (node == ranges.end()) return 0;
    const LinkPartRanges& r = node->second;
    const VertexPacket* positions = vertices.data() + r.vertices.start;
    if (!offsets->empty()) positions = MovedPositions(positions, r.vertices.count, offsets);
    ProjectFlat(matrix, positions, points, r.vertices.count, bounds);
    return r.vertices.count << 2;
}
