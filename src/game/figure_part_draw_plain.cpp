// The plain draw of one figure part (005A7640, thiscall on the part's scene node, RET 0x18): the
// simpler sibling of 005A7A70 (figure_part_draw.cpp), always through the device and without the
// lighting: a hidden mesh (flag 0x80) is skipped, the drawn flag (0x40) cleared, the mesh
// clip-tested against the view, animated meshes (type 13..16) advanced to the current time; flat
// meshes (0x100) get their colours (+0x138) filled with +0xD4 and speculars (+0x148) with +0xD8
// (the arrays keep their sizes), a mesh not yet lit (no 0x10) asks the device to light it (draw
// flag 4); the per-vertex offsets (+0x128, +0x12C of them) are added into a copy of the positions
// (its own function-local static 007C2E60, guard 007C2E70 bit 1, freed at exit by 005A7A50). Any
// draw that is not off screen marks the mesh drawn. See docs/notes/models.md ("Figure parts").
#include "ei/hook.h"
#include "figure_mesh.h"          // VertexPacket
#include "link_figure.h"          // FigureMeshDrawer
#include "lib/containers.h"
#include "lib/gameapi.h"
#include "model.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_render.h"
#include "render/render.h"   // MeshSource
#include "texture.h"           // CTextureDevice
#include "world.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

// The figure mesh's colour arrays (+0x138 / +0x148).
using ColourArray = lib::AlignedArray<std::uint32_t>;

// The moved positions (lib::AlignedArray; its inlined SetSize allocates through 00452E70).
lib::AlignedArray<VertexPacket>& MovedPositions() { return *reinterpret_cast<lib::AlignedArray<VertexPacket>*>(0x007C2E60); }
std::uint8_t& Guard() { return *reinterpret_cast<std::uint8_t*>(0x007C2E70); }

// 004F16A0 CTransformPipeline::ClipTest (model.cpp): 2 = outside the view, 1 = crossing it.
// 005A7160: the node's animation at a time (float bits). 005A7290: after the draw (the renderer).
// 004E9010: the device's mesh draw (device_mesh.cpp).

// *target = *offset + *target (FLD offset, FADD, FSTP).
void AddOffset(const float* offset, float* target) { *target = *offset + *target; }

// The world time: whole ticks (FILD) plus the fraction of the current one, rounded to a float.
void TickTime(float* out) {
    const CWorldClient* world = GameClientWorld();
    *out = static_cast<float>(static_cast<double>(world->tick_) + world->tickFraction_);
}

void Fill(ColourArray& a, std::uint32_t value) {
    for (int i = 0; i < a.size; ++i) a.data[i] = value;
}

// The positions with the per-vertex offsets added, in 007C2E60.
const void* MovedPositionsOf(CFigureMesh* mesh) {
    lib::AlignedArray<VertexPacket>& moved = MovedPositions();
    int blocks = mesh->buffers_.vertexCount;
    moved.SetSize(blocks);
    if (blocks) std::memcpy(moved.data, mesh->buffers_.vertices, static_cast<std::size_t>(blocks) * 64);
    for (int i = 0; i < mesh->offsets_.size; ++i) {
        const float* offset = &mesh->offsets_.data[i].x;
        VertexPacket& packet = moved.data[i >> 2];
        AddOffset(&offset[0], &packet.block[0][i & 3]);
        AddOffset(&offset[1], &packet.block[1][i & 3]);
        AddOffset(&offset[2], &packet.block[2][i & 3]);
    }
    return moved.data;
}

std::uint32_t Word(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

// The flags an animated mesh's drawer gets (00790C0C).
std::uint32_t AnimatedDrawerFlags() { return *reinterpret_cast<const std::uint32_t*>(0x00790C0C); }

} // namespace

// 005A7640 (thiscall on the part's drawer, RET 0x18). Arguments 2-4 are not read; `alpha` goes to
// the device draw, `rect` (optional) receives the screen rectangle (empty when off screen).
// Returns 1.
int FigureMeshDrawer::DrawPlain(CTextureDevice* device, int, int, int, int alpha,
                                   RECT* rect) {
    if (mesh_->flags_ & 0x80) return 1;
    if (!(Guard() & 1)) {
        Guard() |= 1;
        MovedPositions() = lib::AlignedArray<VertexPacket>{};
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x005A7A50));
    }
    int drawFlags = 0;
    mesh_->flags_ &= ~0x40u;
    int visibility = device->pipeline_->ClipTest(&mesh_->bounds_);
    if (visibility == 2) return 1;
    int type = static_cast<int>(mesh_->meshType_);
    if (type >= 13 && type <= 16) {
        float time;
        TickTime(&time);
        flags_ |= AnimatedDrawerFlags();
        Animate(time);
    }

    std::uint32_t meshFlags = mesh_->flags_;
    if (meshFlags & 0x100) {
        Fill(mesh_->colorsA_, mesh_->flatColour_);
        Fill(mesh_->colorsB_, mesh_->flatSpecular_);
    } else if (!(meshFlags & 0x10)) {
        drawFlags = 4;
    }
    if (visibility == 1) drawFlags |= 2;
    if (rect) drawFlags |= 1;

    const void* positions = mesh_->buffers_.vertices;
    int blocks = mesh_->buffers_.vertexCount;
    if (mesh_->offsets_.size != 0) positions = MovedPositionsOf(mesh_);
    CFigureMesh* m = mesh_;
    const MeshBuffers& b = m->buffers_;
    // The new renderer keeps the mesh on the GPU (R16d): what the device's draw below draws.
    const render::MeshSource source{m, nullptr, m->offsets_.size != 0, (meshFlags & 0x100) != 0};
    render::SetMeshSource(&source);
    int result = DrawMesh(reinterpret_cast<char*>(device), nullptr, positions, blocks, Word(b.normals), b.normalCount,
                          Word(m->colorsA_.data), Word(m->colorsB_.data), Word(b.texCoords),
                          static_cast<std::uint32_t>(b.texCoordCount), Word(b.corners),
                          static_cast<std::uint32_t>(b.cornerCount), Word(b.words), static_cast<std::uint32_t>(b.wordCount),
                          Word(b.indices), static_cast<std::uint32_t>(b.indexCount), std::bit_cast<std::uint32_t>(m->textureOffsetU_),
                          std::bit_cast<std::uint32_t>(m->textureOffsetV_), alpha, drawFlags);
    render::SetMeshSource(nullptr);
    if (rect) {
        RECT* own = &screen_;
        if (result == 2) {
            gameapi::SetRectEmpty(own);
            gameapi::SetRectEmpty(rect);
        } else {
            gameapi::CopyRect(own, &device->drawnRect_);
            *rect = *own;
        }
    }
    if (result != 2) mesh_->flags_ |= 0x40;
    DrawBounds(reinterpret_cast<CSceneRenderer*>(0x007AF060));
    return 1;
}
EI_REPLACE_METHOD(0x005A7640, FigureMeshDrawer::DrawPlain);
