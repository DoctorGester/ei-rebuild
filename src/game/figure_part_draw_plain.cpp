// The plain draw of one figure part: the lit draw's simpler sibling, always through the device and without the
// lighting. Flat meshes (0x100) get their colours filled with one value and speculars with another, a mesh not
// yet lit asks the device to light it, and the per-vertex offsets are added into a copy of the positions. See
// docs/notes/models.md ("Figure parts").
#include "ei/hook.h"
#include "figure_mesh.h"
#include "link_figure.h"
#include "lib/containers.h"
#include "lib/gameapi.h"
#include "model.h"
#include "render/render.h"
#include "texture.h"
#include "world.h"
#include "game_data.h"
#include "game_singletons.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

using ColourArray = lib::AlignedArray<std::uint32_t>;

constinit lib::AlignedArray<VertexPacket> g_movedPositions = /*007C2E60*/ {};
lib::AlignedArray<VertexPacket>& MovedPositions() { return g_movedPositions; }
void __cdecl FreeMovedPositions() { g_movedPositions.FreeData(); }
std::uint8_t g_movedPositionsGuard = /*007C2E70*/ {};   // bit 0: constructed

void AddOffset(const float* offset, float* target) { *target = *offset + *target; }

// The world time: whole ticks plus the fraction of the current one, rounded to a float.
void TickTime(float* out) {
    const CWorldClient* world = TheClientWorld();
    *out = static_cast<float>(static_cast<double>(world->tick_) + world->tickFraction_);
}

void Fill(ColourArray& a, std::uint32_t value) {
    for (int i = 0; i < a.size; ++i) a.data[i] = value;
}

const VertexPacket* MovedPositionsOf(CFigureMesh* mesh) {
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

} // namespace

// Arguments 2-4 are not read; `alpha` goes to the device draw, `rect` (optional) receives the screen rectangle
// (empty when off screen). Returns 1.
int FigureMeshDrawer::DrawPlain(CTextureDevice* device, const void*, const void*, CMaterial*, int alpha,
                                   RECT* rect) {
    if (mesh_->flags_ & 0x80) return 1;
    if (!(g_movedPositionsGuard & 1)) {
        g_movedPositionsGuard |= 1;
        MovedPositions() = lib::AlignedArray<VertexPacket>{};
        libcrt::AtExit(FreeMovedPositions);
    }
    int drawFlags = 0;
    mesh_->flags_ &= ~0x40u;
    int visibility = device->pipeline_->ClipTest(&mesh_->box_.bounds);
    if (visibility == 2) return 1;
    int type = static_cast<int>(mesh_->meshType_);
    if (type >= 13 && type <= 16) {
        float time;
        TickTime(&time);
        flags_ |= kAnimatedDrawerFlags;
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

    const VertexPacket* positions = mesh_->buffers_.vertices;
    int blocks = mesh_->buffers_.vertexCount;
    if (mesh_->offsets_.size != 0) positions = MovedPositionsOf(mesh_);
    CFigureMesh* m = mesh_;
    const MeshBuffers& b = m->buffers_;
    // The new renderer keeps the mesh on the GPU: what the device's draw below draws.
    const render::MeshSource source{m, nullptr, m->offsets_.size != 0, (meshFlags & 0x100) != 0};
    render::SetMeshSource(&source);
    int result = device->DrawMeshBlocks(positions, blocks, b.normals, b.normalCount, m->colorsA_.data, m->colorsB_.data,
                                        b.texCoords, b.texCoordCount, b.corners, b.cornerCount, b.words, b.wordCount,
                                        b.indices, b.indexCount, m->textureOffsetU_,
                                        m->textureOffsetV_, alpha, drawFlags);
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
    DrawBounds(TheSceneRenderer());
    return 1;
}
EI_REWRITTEN(0x005A7640, FigureMeshDrawer::DrawPlain);
EI_REWRITTEN(0x005A7A50, FreeMovedPositions);
