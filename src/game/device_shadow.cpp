// The device's draw of flat-coloured textured geometry, used for object shadows: the positions are projected by
// the pipeline (the visibility verdict); the renderer gets one colour / specular pair, each vertex's texture
// coordinate and a z scale, and clips what crosses the screen edges. See docs/notes/models.md ("Mesh draws").
#include "device_buffers.h"
#include "figure_mesh.h"   // VertexPacket
#include "model.h"
#include "render/render.h"
#include "texture.h"

#include "lib/nan_audit.h"

#include <cstdint>

namespace {

using devicebuffers::g_outcodes;
using devicebuffers::g_projected;

// The pipeline's screen stack (1; the projection uses it or the clip stack 2, which get the same pushes).
constexpr int kScreenStack = 1;

// The new renderer (src/render) gets the geometry in model space with the screen stack's matrix
// (kScreenStack); each pass the indexed draw then makes hands over the device's state.
void HandOver(CTextureDevice* device, const VertexPacket* positions, const void* uvs, int count,
              const std::uint16_t* indices, int indexCount, const std::uint32_t* colours, bool clipped) {
    render::MeshDraw m{};
    m.positions = &positions->block[0][0];
    m.positionCount = count;
    m.colours = &colours[0];
    m.speculars = &colours[1];
    m.uvs = static_cast<const float*>(uvs);
    m.corners = nullptr;
    m.cornerCount = count;
    m.indices = indices;
    m.indexCount = indexCount;
    m.matrix = device->pipeline_->Top(kScreenStack);
    m.screenMatrix = true;
    m.clipped = clipped;
    m.distanceFog = false;   // no fog pass here
    m.zScale = 1.0f;   // multiplies z (1: none)
    render::BeginMesh(device, m);
}

int DrawFlat(CTextureDevice* device, const VertexPacket* positions, const void* uvs, int blocks,
             const std::uint16_t* indices, int indexCount, std::uint32_t colour, std::uint32_t specular) {
    CTransformPipeline* pipeline = device->pipeline_;
    int count = blocks * 4;
    std::uint32_t colours[2] = {colour, specular};
    g_projected.resize(count);
    g_outcodes.resize(count);
    // The verdict: 0 on screen, 1 crossing, 2 outside.
    int visibility = pipeline->ProjectBlocks(positions, g_projected.data(), g_outcodes.data(), blocks, nullptr);
    if (visibility == 2) return 2;
    HandOver(device, positions, uvs, count, indices, indexCount, colours, visibility == 1);
    device->DrawIndexed();
    return 0;
}

} // namespace

// `positions` blocks of four vertices (`blocks` of them), `uvs` one (u, v) per vertex,
// `indices` / `indexCount` the triangles, `colour` and `specular` for every vertex. Returns 2 when
// nothing is visible, else 0.
int CTextureDevice::DrawFlatBlocks(const VertexPacket* positions, const void* uvs, int blocks,
                                   const std::uint16_t* indices, int indexCount, std::uint32_t colour,
                                   std::uint32_t specular) {
    render::BeginFlatDrawTime(blocks * 4);
    int result = DrawFlat(this, positions, uvs, blocks, indices, indexCount, colour, specular);
    render::EndMesh();
    render::EndFlatDrawTime();
    return result;
}
