// CLinkMesh's draw slots (link_figure.h; vtable 0073D790, slots 7-14; A-S12): a part of a figure
// coloured (004F3AD0), lit (004F3870), drawn through the device's mesh draws (004F3C60, 004F3FA0,
// 004F42D0, 004F4150) and, for its shadow, projected (004F4680) and rasterised (004F4850). The
// callers are the figure part draw 005A7A70 (figure_part_draw.cpp) and the shadow 0051CE60
// (map_object_shadow.cpp), through the vtable only. The old Direct3D 7 path: the two plain draws
// turn the device's specular on around the draw for a material that keeps its speculars.
#include "device.h"   // the old device's calls
#include "library_trees.h"
#include "link_figure.h"

#include "core_maths.h"
#include "material.h"            // CMaterial
#include "texture.h"             // CTextureDevice, CPixelConverter

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "render/render.h"         // MeshChanged
#include "render/render_state.h"
#include "game_data.h"

#include <d3d.h>

#include <cstdint>
#include <cstring>

namespace {

using ColourArray = lib::AlignedArray<std::uint32_t>;

constexpr int kDrawScreenRect = 1;    // draw flag: the part's screen rectangle is kept
constexpr int kDrawSpecular = 8;      // draw flag: the material keeps its speculars
constexpr std::uint8_t kMaterialKeepsSpecular = 0x10;
constexpr int kOpaque = 0xFF;         // the alpha the draws pass (read with draw flag 4 only)
constexpr int kNothingDrawn = 2;      // the device's result, also for a part the figure does not have
constexpr float kByteScale = 255.0f;  // 0073C370

// The positions of a part moved by its per-vertex offsets (007AEED0: one work array for the draws
// and the shadow's projection).
// Zero at the start, as the static initialiser 004F2AF0 leaves it; its exit function frees the block.
constinit lib::AlignedArray<VertexPacket> g_movedPackets = /*007AEED0*/ {};
lib::AlignedArray<VertexPacket>& MovedPackets() { return g_movedPackets; }
void __cdecl FreeMovedPackets() { g_movedPackets.FreeData(); }   // 004F2B20

// A screen or texture point of the shadow's rasteriser (moved as one qword; texture.h).
using PixelPair = RasterTriangle::Point;

// A part's texture coordinates in 0..255 (a local static of 004F4850: bit 0 of its guard says it is
// constructed, and the exit handler 004F4B30 frees its block).
constinit lib::AlignedArray<RasterTriangle::Point> g_texturePixels = /*007AEEB8*/ {};
std::uint8_t g_texturePixelsGuard = /*007AEEC8*/ {};
void __cdecl FreeTexturePixels() { g_texturePixels.FreeData(); }   // 004F4B30
lib::AlignedArray<PixelPair>& TexturePixels() {
    auto& constructed = g_texturePixelsGuard;
    auto& pixels = g_texturePixels;
    if (!(constructed & 1)) {
        constructed |= 1;
        pixels.Construct();
        libcrt::AtExit(FreeTexturePixels);   // 004F4B30
    }
    return pixels;
}

// The screen rectangle of a part that was not drawn.
constexpr RECT kNoScreen = /*0073D780*/ {0, 0, 0, 0};

void Fill(ColourArray& array, std::uint32_t value) {
    for (int i = 0; i < array.size; ++i) array.data[i] = value;
}

// Vertex i of `packets` (component i & 3 of packet i >> 2), coordinate `axis`.
float& Coordinate(VertexPacket* packets, int i, int axis) { return packets[i >> 2].block[axis][i & 3]; }

// `source` (`packets` packets) with the offsets added, in the work array: a vertex beyond the
// offsets is copied; the w row is not written.
const VertexPacket* MovedPositions(const VertexPacket* source, int packets, const lib::AlignedArray<Vector3>* offsets) {
    auto& moved = MovedPackets();
    moved.SetSizeDiscard(packets);
    auto* from = const_cast<VertexPacket*>(source);
    for (int i = 0; i < moved.size * 4; ++i) {
        if (i < offsets->size) {
            const Vector3& offset = offsets->data[i];
            Coordinate(moved.data, i, 0) = offset.x + Coordinate(from, i, 0);
            Coordinate(moved.data, i, 1) = offset.y + Coordinate(from, i, 1);
            Coordinate(moved.data, i, 2) = offset.z + Coordinate(from, i, 2);
        } else {
            for (int axis = 0; axis < 3; ++axis)
                std::memcpy(&Coordinate(moved.data, i, axis), &Coordinate(from, i, axis), sizeof(float));
        }
    }
    return moved.data;
}

// IDirect3DDevice7::SetRenderState(D3DRENDERSTATE_SPECULARENABLE); the result is not kept.
void SetSpecular(CTextureDevice* device, bool on) {
    device::SetRenderState(device->d3dDevice_, D3DRENDERSTATE_SPECULARENABLE, on ? 1 : 0);
    render::state::Render(D3DRENDERSTATE_SPECULARENABLE, on ? 1 : 0);
}

// What a draw needs of the mesh: a part's ranges, resolved to the device's mesh draw arguments.
struct PartDraw {
    CLinkMesh* mesh;
    const LinkPartRanges& r;
    const VertexPacket* positions;
    ColourArray* colours;
    ColourArray* speculars;
    std::uint32_t u;
    std::uint32_t v;
    int flags;

    // 004E9010 / 004E9A50 (CTextureDevice::DrawMeshBlocks / 2): positions and
    // packets, normals and count, colours, speculars, texture coordinates and count, corners and
    // count, words and count, indices and count, the texture offsets, alpha, flags.
    int Draw(CTextureDevice* device, CDXSurface* first, CDXSurface* second, bool textured) const {
        auto* indices = reinterpret_cast<std::uint16_t*>(mesh->indices_.data + r.indices.start);
        const auto* words = mesh->words_.data + r.words.start;
        const auto* corners = mesh->corners_.data + r.corners.start;
        const auto* texCoords = mesh->texCoords_.data + r.texCoords.start;
        const auto* normals = mesh->normals_.data + r.normals.start;
        const float uOffset = std::bit_cast<float>(u);
        const float vOffset = std::bit_cast<float>(v);
        if (textured) {
            return device->DrawMeshBlocks2(first, second, positions, r.vertices.count, normals, r.normals.count,
                                           colours->data, speculars->data, texCoords, r.texCoords.count, corners,
                                           r.corners.count, words, r.words.count, indices, r.indices.count, uOffset,
                                           vOffset, kOpaque, flags);
        }
        return device->DrawMeshBlocks(positions, r.vertices.count, normals, r.normals.count, colours->data,
                                      speculars->data, texCoords, r.texCoords.count, corners, r.corners.count, words,
                                      r.words.count, indices, r.indices.count, uOffset, vOffset, kOpaque, flags);
    }
};

// After a draw: the part's header takes the rectangle the device drew (draw flag 1, something
// drawn) or the empty one.
void KeepScreen(CLinkMesh* mesh, const stlp::string* name, int flags, int result) {
    if ((flags & kDrawScreenRect) && result != kNothingDrawn) {
        const RECT& drawn = mesh->loader_->renderer_->drawnRect_;
        static_cast<lib_trees::LinkHeaders*>(&mesh->headers_)->Subscript(*name)->screen = drawn;
        return;
    }
    static_cast<lib_trees::LinkHeaders*>(&mesh->headers_)->Subscript(*name)->screen = kNoScreen;
}

// The four draws: `offsets` (null: none asked) moves the positions when it has any; without
// surfaces the device's specular is on around the draw for draw flag 8.
int DrawPartCommon(CLinkMesh* mesh, const stlp::string* name, int flags, CDXSurface* first, CDXSurface* second,
                   bool textured, ColourArray* colours, ColourArray* speculars,
                   const lib::AlignedArray<Vector3>* offsets, std::uint32_t u, std::uint32_t v) {
    const LinkRangeMap::Node* node = mesh->ranges_.find(*name);
    if (node == mesh->ranges_.end()) return kNothingDrawn;
    const LinkPartRanges& r = node->second;
    const VertexPacket* positions = mesh->vertices_.data + r.vertices.start;
    if (offsets && offsets->size != 0) positions = MovedPositions(positions, r.vertices.count, offsets);

    CTextureDevice* device = mesh->loader_->renderer_;
    const bool specular = !textured && (flags & kDrawSpecular) != 0;
    if (specular) SetSpecular(device, true);
    const PartDraw draw{mesh, r, positions, colours, speculars, u, v, flags};
    const int result = draw.Draw(mesh->loader_->renderer_, first, second, textured);
    if (specular) SetSpecular(device, false);
    KeepScreen(mesh, name, flags, result);
    return result;
}

} // namespace

// ---- 7 / 8: the colours ----------------------------------------------------------------------------

// 004F3AD0 (slot 7, RET 0x14, AL). Both arrays sized to four per normal packet (their contents
// dropped), then filled.
bool CLinkMesh::FlatColoursImpl(const stlp::string* name, std::uint32_t colour, std::uint32_t specular,
                            ColourArray* colours, ColourArray* speculars) {
    const LinkRangeMap::Node* node = ranges_.find(*name);
    if (node == ranges_.end()) return false;
    colours->SetSizeDiscard(node->second.normals.count * 4);
    speculars->SetSizeDiscard(node->second.normals.count * 4);
    Fill(*colours, colour);
    Fill(*speculars, specular);
    render::MeshChanged(colours->data, render::MeshChange::Colours);   // the colours the new renderer keeps (R16d)
    return true;
}
EI_REWRITTEN(0x004F3AD0, CLinkMesh::FlatColoursImpl);

// 004F3870 (slot 8, RET 0x18, AL). The arrays sized as above; the colours the part lit on the GPU keeps
// (R16i: CMaterial::GpuLitColour; the transform object's LightMesh lit them before); every specular is the alpha
// byte in the top byte, over the material's specular colour (components times 255, truncated by
// _ftol and OR-ed in unmasked) when the material keeps its speculars.
bool CLinkMesh::LightPartImpl(const stlp::string* name, const FigureLights* lights, CMaterial* material, int alpha,
                          ColourArray* colours, ColourArray* speculars) {
    const LinkRangeMap::Node* node = ranges_.find(*name);
    if (node == ranges_.end()) return false;
    const LinkPartRanges& r = node->second;
    colours->SetSizeDiscard(r.normals.count * 4);
    speculars->SetSizeDiscard(r.normals.count * 4);

    (void)lights;   // the GPU's (figure_part_draw.cpp, PartLighting)
    Fill(*colours, material->GpuLitColour());

    const std::uint32_t alphaByte = static_cast<std::uint32_t>(alpha) & 0xFF;
    if (material->flags_ & kMaterialKeepsSpecular) {
        std::uint32_t packed = gamecrt::Ftol(material->colors.specular.x * kByteScale) | (alphaByte << 8);
        packed <<= 8;
        packed |= gamecrt::Ftol(material->colors.specular.y * kByteScale);
        packed <<= 8;
        packed |= gamecrt::Ftol(material->colors.specular.z * kByteScale);
        Fill(*speculars, packed);
    } else {
        Fill(*speculars, alphaByte << 24);
    }
    render::MeshChanged(colours->data, render::MeshChange::Colours);   // the colours the new renderer keeps (R16d)
    return true;
}
EI_REWRITTEN(0x004F3870, CLinkMesh::LightPartImpl);

// ---- 9-12: the draws -------------------------------------------------------------------------------

// 004F3C60 (slot 9, RET 0x1C).
int CLinkMesh::DrawPartMovedImpl(const stlp::string* name, int flags, ColourArray* colours, ColourArray* speculars,
                             lib::AlignedArray<Vector3>* offsets, std::uint32_t u, std::uint32_t v) {
    return DrawPartCommon(this, name, flags, nullptr, nullptr, false, colours, speculars, offsets, u, v);
}
EI_REWRITTEN(0x004F3C60, CLinkMesh::DrawPartMovedImpl);

// 004F3FA0 (slot 10, RET 0x18).
int CLinkMesh::DrawPartImpl(const stlp::string* name, int flags, ColourArray* colours, ColourArray* speculars,
                        std::uint32_t u, std::uint32_t v) {
    return DrawPartCommon(this, name, flags, nullptr, nullptr, false, colours, speculars, nullptr, u, v);
}
EI_REWRITTEN(0x004F3FA0, CLinkMesh::DrawPartImpl);

// 004F42D0 (slot 11, RET 0x24).
int CLinkMesh::DrawPartMovedTexturedImpl(const stlp::string* name, int flags, CDXSurface* first, CDXSurface* second,
                                     ColourArray* colours, ColourArray* speculars,
                                     lib::AlignedArray<Vector3>* offsets, std::uint32_t u, std::uint32_t v) {
    return DrawPartCommon(this, name, flags, first, second, true, colours, speculars, offsets, u, v);
}
EI_REWRITTEN(0x004F42D0, CLinkMesh::DrawPartMovedTexturedImpl);

// 004F4150 (slot 12, RET 0x20).
int CLinkMesh::DrawPartTexturedImpl(const stlp::string* name, int flags, CDXSurface* first, CDXSurface* second,
                                ColourArray* colours, ColourArray* speculars, std::uint32_t u, std::uint32_t v) {
    return DrawPartCommon(this, name, flags, first, second, true, colours, speculars, nullptr, u, v);
}
EI_REWRITTEN(0x004F4150, CLinkMesh::DrawPartTexturedImpl);

// ---- 13 / 14: the shadow ---------------------------------------------------------------------------

// 004F4680 (slot 13, RET 0x14). The part's positions (moved by `offsets` when it has any) through
// the transform object's flat projection; four points per packet.
int CLinkMesh::ProjectPartImpl(const stlp::string* name, lib::AlignedArray<Vector3>* offsets, const float* matrix,
                           Vector2* points, float* bounds) {
    const LinkRangeMap::Node* node = ranges_.find(*name);
    if (node == ranges_.end()) return 0;
    const LinkPartRanges& r = node->second;
    const VertexPacket* positions = vertices_.data + r.vertices.start;
    if (offsets->size != 0) positions = MovedPositions(positions, r.vertices.count, offsets);
    pipeline_->ProjectFlat(matrix, positions, points, r.vertices.count, bounds);
    return r.vertices.count << 2;
}
EI_REWRITTEN(0x004F4680, CLinkMesh::ProjectPartImpl);

// 004F4850 (slot 14, RET 0xC). The part's texture coordinates times 255 (as floats, rounded to the
// nearest integer), then every triangle's corners - the projected point of the corner's position
// and the texture point of its coordinate - handed to the pixel converter's rasteriser at the
// start of `target`.
int CLinkMesh::RasterisePartImpl(const stlp::string* name, const PixelPair* pixels, RasterTarget* target) {
    const LinkRangeMap::Node* node = ranges_.find(*name);
    if (node == ranges_.end()) return 0;
    const LinkPartRanges& r = node->second;
    lib::AlignedArray<PixelPair>& texturePixels = TexturePixels();
    CPixelConverter* converter = static_cast<CPixelConverter*>(loader_->renderer_->converter_);

    texturePixels.SetSizeDiscard(r.texCoords.count);
    for (unsigned i = 0; i < static_cast<unsigned>(r.texCoords.count); ++i) {
        float uv[2];
        std::memcpy(uv, &texCoords_.data[r.texCoords.start + static_cast<int>(i)], sizeof(uv));
        texturePixels.data[i].x = x87::fistp(static_cast<float>(uv[0] * kByteScale));
        texturePixels.data[i].y = x87::fistp(static_cast<float>(uv[1] * kByteScale));
    }

    const MeshCorner* corners = corners_.data + r.corners.start;
    const std::uint16_t* indices = indices_.data + r.indices.start;
    for (unsigned n = 0; n < static_cast<unsigned>(r.indices.count); n += 3) {
        for (int k = 0; k < 3; ++k) {
            const MeshCorner& corner = corners[indices[n + k]];
            std::memcpy(&target->corners[k].screen, &pixels[corner.index[0]], sizeof(PixelPair));
            std::memcpy(&target->corners[k].texture, &texturePixels.data[corner.index[2]], sizeof(PixelPair));
        }
        converter->RasteriseTriangle(target);
    }
    return r.vertices.count << 2;
}
EI_REWRITTEN(0x004F4850, CLinkMesh::RasterisePartImpl);
EI_REPLACE(0x004F4B30, FreeTexturePixels);
EI_REPLACE(0x004F2B20, FreeMovedPackets);   // the exit function of 004F2AF0
