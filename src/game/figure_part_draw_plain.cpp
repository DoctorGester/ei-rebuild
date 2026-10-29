// The plain draw of one figure part: the lit draw's simpler sibling, always through the device and without the
// lighting. Flat meshes (0x100) get their colours filled with one value and speculars with another, a mesh not
// yet lit asks the device to light it, and the per-vertex offsets are added into a copy of the positions. See
// docs/notes/models.md ("Figure parts").
#include "figure_mesh.h"
#include "link_figure.h"
#include "lib/containers.h"
#include "lib/gameapi.h"
#include "model.h"
#include "render/render.h"
#include "texture.h"
#include "world.h"
#include "game_singletons.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// The positions with the offsets added.
std::vector<VertexPacket> g_movedPositions;

void AddOffset(const float* offset, float* target) { *target = *offset + *target; }

// The world time: whole ticks plus the fraction of the current one, rounded to a float.
void TickTime(float* out) {
    const WorldClient* world = &g_clientWorld;
    *out = static_cast<float>(static_cast<double>(world->tick) + world->tickFraction);
}

void Fill(std::vector<std::uint32_t>& a, std::uint32_t value) {
    for (int i = 0; i < std::ssize(a); ++i) a[i] = value;
}

const VertexPacket* MovedPositionsOf(FigureMesh* mesh) {
    std::vector<VertexPacket>& moved = g_movedPositions;
    int blocks = mesh->buffers.vertexCount;
    moved.resize(blocks);
    if (blocks) std::memcpy(moved.data(), mesh->buffers.vertices, static_cast<std::size_t>(blocks) * 64);
    for (int i = 0; i < std::ssize(mesh->offsets); ++i) {
        const float* offset = &mesh->offsets[i].x;
        VertexPacket& packet = moved[i >> 2];
        AddOffset(&offset[0], &packet.block[0][i & 3]);
        AddOffset(&offset[1], &packet.block[1][i & 3]);
        AddOffset(&offset[2], &packet.block[2][i & 3]);
    }
    return moved.data();
}

} // namespace

// Arguments 2-4 are not read; `alpha` goes to the device draw, `rect` (optional) receives the screen rectangle
// (empty when off screen). Returns 1.
int FigureMeshDrawer::DrawPlain(TextureDevice* device, Material*, int alpha,
                                   RECT* rect) {
    if (mesh->flags & FigureMeshFlag::Hidden) return 1;
    int drawFlags = 0;
    mesh->flags &= ~FigureMeshFlag::Drawn;
    int visibility = device->pipeline->ClipTest(&mesh->box.bounds);
    if (visibility == 2) return 1;
    int meshType = static_cast<int>(mesh->meshType);
    if (meshType >= 13 && meshType <= 16) {
        float time;
        TickTime(&time);
        flags |= kAnimatedDrawerFlags;
        Animate(time);
    }

    std::uint32_t meshFlags = mesh->flags;
    if (meshFlags & FigureMeshFlag::Flat) {
        Fill(mesh->colorsA, mesh->flatColour);
        Fill(mesh->colorsB, mesh->flatSpecular);
    } else if (!(meshFlags & FigureMeshFlag::Lit)) {
        drawFlags = 4;
    }
    if (visibility == 1) drawFlags |= 2;
    if (rect) drawFlags |= 1;

    const VertexPacket* positions = mesh->buffers.vertices;
    int blocks = mesh->buffers.vertexCount;
    if (!mesh->offsets.empty()) positions = MovedPositionsOf(mesh);
    FigureMesh* m = mesh;
    const MeshBuffers& b = m->buffers;
    // The new renderer keeps the mesh on the GPU: what the device's draw below draws.
    const render::MeshSource source{m, nullptr, !m->offsets.empty(), lib::Has(meshFlags, FigureMeshFlag::Flat)};
    render::SetMeshSource(&source);
    int result = device->DrawMeshBlocks(positions, blocks, b.normals, b.normalCount, m->colorsA.data(), m->colorsB.data(),
                                        b.texCoords, b.texCoordCount, b.corners, b.cornerCount, b.words, b.wordCount,
                                        b.indices, b.indexCount, m->textureOffsetU,
                                        m->textureOffsetV, alpha, drawFlags);
    render::SetMeshSource(nullptr);
    if (rect) {
        RECT* own = &screen;
        if (result == 2) {
            gameapi::SetRectEmpty(own);
            gameapi::SetRectEmpty(rect);
        } else {
            gameapi::CopyRect(own, &device->drawnRect);
            *rect = *own;
        }
    }
    if (result != 2) mesh->flags |= FigureMeshFlag::Drawn;
    DrawBounds(&g_sceneRenderer);
    return 1;
}
