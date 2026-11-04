// LinkMesh's draw slots (link_figure.h): a part of a figure
// coloured, lit, drawn through the device's mesh draws and, for its shadow, projected and rasterised. The
// callers are the figure part draw (figure_part_draw.cpp) and the shadow
// (map_object_shadow.cpp). The old Direct3D 7 path: the two plain draws
// turn the device's specular on around the draw for a material that keeps its speculars.
#include "core_maths.h"
#include "link_figure.h"
#include "material.h"        // Material
#include "render/render.h"   // MeshChanged
#include "render/render_state.h"
#include "texture.h"   // TextureDevice

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint8_t kMaterialKeepsSpecular = 0x10;
constexpr int kOpaque = 0xFF;         // the alpha the draws pass (read with kDrawColours only)
constexpr int kNothingDrawn = TransformPipeline::kClipOutside;   // the device's result, also for a part the figure does not have
constexpr float kByteScale = 255.0f;

// The original's _ftol: truncated through 64 bits, the low half (0 for a NaN or a value beyond the range).
std::uint32_t Truncated(float value) { return static_cast<std::uint32_t>(static_cast<std::int64_t>(value)); }

// The screen rectangle of a part that was not drawn.
constexpr Rect kNoScreen = /*0073D780*/ {0, 0, 0, 0};

void Fill(std::vector<std::uint32_t>& array, std::uint32_t value) {
    for (int i = 0; i < std::ssize(array); ++i) array[i] = value;
}

// What a draw needs of the mesh: a part's ranges, resolved to the device's mesh draw arguments.
struct PartDraw {
    LinkMesh* mesh;
    const LinkPartRanges& r;
    const VertexPacket* positions;
    std::vector<std::uint32_t>* colours;
    std::vector<std::uint32_t>* speculars;
    float u;
    float v;
    int flags;

    // TextureDevice::DrawMeshBlocks / 2.
    int Draw(TextureDevice* device, Surface* first, Surface* second, bool textured) const {
        std::uint16_t* indices = mesh->indices.data() + r.indices.start;
        const auto* words = mesh->words.data() + r.words.start;
        const auto* corners = mesh->corners.data() + r.corners.start;
        const auto* texCoords = mesh->texCoords.data() + r.texCoords.start;
        const auto* normals = mesh->normals.data() + r.normals.start;
        const MeshArgs args{positions, r.vertices.count, normals, r.normals.count, colours->data(), speculars->data(),
                            texCoords, corners, r.corners.count, words, r.words.count, indices, r.indices.count,
                            u, v, kOpaque, flags};
        return textured ? device->DrawMeshBlocks2(first, second, args) : device->DrawMeshBlocks(args);
    }
};

// After a draw: the part's header takes the rectangle the device drew (kDrawScreenRect, something
// drawn) or the empty one.
void KeepScreen(LinkMesh* mesh, std::string_view name, int flags, int result) {
    if ((flags & kDrawScreenRect) && result != kNothingDrawn) {
        const Rect& drawn = mesh->loader->renderer->drawnRect;
        mesh->headers[std::string(name)].screen = drawn;
        return;
    }
    mesh->headers[std::string(name)].screen = kNoScreen;
}

// The four draws: `offsets` (null: none asked) moves the positions when it has any; without
// surfaces the device's specular is on around the draw for kDrawSpecular.
int DrawPartCommon(LinkMesh* mesh, std::string_view name, int flags, Surface* first, Surface* second,
                   bool textured, std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars,
                   const std::vector<Vector3>* offsets, float u, float v) {
    auto node = mesh->ranges.find(name);
    if (node == mesh->ranges.end()) return kNothingDrawn;
    const LinkPartRanges& r = node->second;
    const VertexPacket* positions = mesh->vertices.data() + r.vertices.start;
    if (offsets && !offsets->empty()) positions = MovedPositions(positions, r.vertices.count, *offsets);

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
bool LinkMesh::FlatColours(std::string_view partName, std::uint32_t colour, std::uint32_t specular,
                            std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars) {
    auto node = ranges.find(partName);
    if (node == ranges.end()) return false;
    colours->resize(node->second.normals.count * 4);
    speculars->resize(node->second.normals.count * 4);
    Fill(*colours, colour);
    Fill(*speculars, specular);
    render::MeshChanged(colours->data(), render::MeshChange::Colours);   // the colours the renderer keeps
    return true;
}

// The arrays sized as above; the colours the part lit on the GPU keeps
// (Material::GpuLitColour; the transform object's LightMesh lit them before); every specular is the alpha
// byte in the top byte, over the material's specular colour (components times 255, truncated by
// _ftol and OR-ed in unmasked) when the material keeps its speculars.
bool LinkMesh::LightPart(std::string_view partName, const std::vector<Light*>* lights, Material* material, int alpha,
                          std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars) {
    auto node = ranges.find(partName);
    if (node == ranges.end()) return false;
    const LinkPartRanges& r = node->second;
    colours->resize(r.normals.count * 4);
    speculars->resize(r.normals.count * 4);

    (void)lights;   // the GPU's (figure_part_draw.cpp, PartLighting)
    Fill(*colours, material->GpuLitColour());

    const std::uint32_t alphaByte = static_cast<std::uint32_t>(alpha) & 0xFF;
    if (material->flags & kMaterialKeepsSpecular) {
        std::uint32_t packed = Truncated(material->colors.specular.x * kByteScale) | (alphaByte << 8);
        packed <<= 8;
        packed |= Truncated(material->colors.specular.y * kByteScale);
        packed <<= 8;
        packed |= Truncated(material->colors.specular.z * kByteScale);
        Fill(*speculars, packed);
    } else {
        Fill(*speculars, alphaByte << 24);
    }
    render::MeshChanged(colours->data(), render::MeshChange::Colours);   // the colours the renderer keeps
    return true;
}

// ---- 9-12: the draws -------------------------------------------------------------------------------

int LinkMesh::DrawPartMoved(std::string_view partName, int flags, std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars,
                             std::vector<Vector3>* offsets, float u, float v) {
    return DrawPartCommon(this, partName, flags, nullptr, nullptr, false, colours, speculars, offsets, u, v);
}

int LinkMesh::DrawPart(std::string_view partName, int flags, std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars,
                        float u, float v) {
    return DrawPartCommon(this, partName, flags, nullptr, nullptr, false, colours, speculars, nullptr, u, v);
}

int LinkMesh::DrawPartMovedTextured(std::string_view partName, int flags, Surface* first, Surface* second,
                                     std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars,
                                     std::vector<Vector3>* offsets, float u, float v) {
    return DrawPartCommon(this, partName, flags, first, second, true, colours, speculars, offsets, u, v);
}

int LinkMesh::DrawPartTextured(std::string_view partName, int flags, Surface* first, Surface* second,
                                std::vector<std::uint32_t>* colours, std::vector<std::uint32_t>* speculars, float u, float v) {
    return DrawPartCommon(this, partName, flags, first, second, true, colours, speculars, nullptr, u, v);
}
