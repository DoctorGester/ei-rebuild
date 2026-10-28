// The device's draw of flat-coloured textured geometry, used for object shadows: the positions are projected by
// the pipeline, every vertex gets one colour / specular pair and its own texture coordinate, z can be scaled;
// crossing the screen edges, the triangles are clipped. See docs/notes/models.md ("Mesh draws").
#include "clip.h"

#include "device_buffers.h"
#include "model.h"
#include "render/render.h"
#include "texture.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <cstring>

namespace {

using devicebuffers::g_clipIndices;
using devicebuffers::g_cornerOutcodes;
using devicebuffers::g_outcodes;
using devicebuffers::g_projected;
using devicebuffers::ProjectedVertex;
using devicebuffers::g_vertices;

// The pipeline's screen stack (1; the projection uses it or the clip stack 2, which get the same pushes).
constexpr int kScreenStack = 1;

void ScaleZ(float* z, const float* scale) { *z = *scale * *z; }

// The vertices: projected position, the colour / specular pair, the vertex's texture coordinate
// (each copied as qwords).
void Fill(TLVertex* out, int count, const void* colours, const void* uvs) {
    const ProjectedVertex* projected = g_projected.data();
    for (int i = 0; i < count; ++i) {
        std::memcpy(&out[i].x, &projected[i], 16);
        std::memcpy(&out[i].color, colours, 8);
        std::memcpy(&out[i].u, static_cast<const std::uint8_t*>(uvs) + i * 8, 8);
    }
}

void Scale(int count, const float* scale) {
    EI_NAN_AUDIT(*scale);
    if (*scale == 1.0f) return;
    for (int i = 0; i < count; ++i) ScaleZ(&g_vertices[i].z, scale);
}

// The new renderer (src/render) gets the geometry in model space with the screen stack's matrix
// (kScreenStack); each pass the indexed draw then makes hands over the device's state.
void HandOver(CTextureDevice* device, const void* positions, const void* uvs, int count, const std::uint16_t* indices,
              int indexCount, const std::uint32_t* colours, const float* zScale, bool clipped) {
    render::MeshDraw m{};
    m.positions = static_cast<const float*>(positions);
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
    m.zScale = *zScale;   // the scale Scale applies (1: none)
    render::BeginMesh(device, m);
}

int DrawFlat(CTextureDevice* device, const void* positions, const void* uvs, int blocks, const std::uint16_t* indices,
             int indexCount, std::uint32_t colour, std::uint32_t specular, const ClipBounds* bounds, float zScale) {
    CTransformPipeline* pipeline = device->pipeline_;
    ITransformPipeline* transform = pipeline->transform_;
    int count = blocks * 4;
    std::uint32_t colours[2] = {colour, specular};
    g_projected.resize(count * 10);
    g_outcodes.resize(count);
    // The blocks projected with outcodes; 0 on screen, 1 crossing, 2 outside.
    int visibility = pipeline->ProjectBlocks(positions, g_projected.data(), g_outcodes.data(), blocks, bounds);
    if (visibility != 2)
        HandOver(device, positions, uvs, count, indices, indexCount, colours, &zScale, visibility == 1);

    if (visibility == 0) {
        // (The device's vertex-buffer batch never gave a buffer: the vertices are drawn.)
        g_vertices.resize(count);
        Fill(g_vertices.data(), count, colours, uvs);
        Scale(count, &zScale);
        device->DrawIndexed(4, 0x1C4, g_vertices.data(), count, indices, indexCount, 0);
        return 0;
    }
    if (visibility == 2) return 2;
    if (visibility != 1) return 0;

    g_vertices.resize(count * 10);
    Fill(g_vertices.data(), count, colours, uvs);
    g_clipIndices.resize(indexCount * 10);
    g_cornerOutcodes.resize(std::ssize(g_outcodes) * 10);
    std::memcpy(g_cornerOutcodes.data(), g_outcodes.data(), static_cast<std::size_t>(std::ssize(g_outcodes)));
    int vertexCount = count;
    int clippedIndices = indexCount;
    transform->ClipTriangles(g_vertices.data(), g_cornerOutcodes.data(), indices, g_clipIndices.data(), &vertexCount,
                             &clippedIndices);
    g_vertices.resize(vertexCount);
    g_clipIndices.resize(clippedIndices);
    transform->ToScreenClipped(g_vertices.data(), std::ssize(g_vertices));
    if (vertexCount == 0 || clippedIndices == 0) return 0;
    Scale(vertexCount, &zScale);
    device->DrawIndexed(4, 0x1C4, g_vertices.data(), vertexCount, g_clipIndices.data(), clippedIndices, 0);
    return 0;
}

} // namespace

// `positions` blocks of four vertices (`blocks` of them), `uvs` one (u, v) per vertex,
// `indices` / `indexCount` the triangles, `colour` and `specular` for every vertex, `bounds` passed
// to the projection (null: crossing), `zScale` multiplies z unless it compares equal to 1. Returns 2 when nothing is
// visible, else 0.
int CTextureDevice::DrawFlatBlocks(const void* positions, const void* uvs, int blocks,
                              const std::uint16_t* indices, int indexCount, std::uint32_t colour, std::uint32_t specular,
                              const ClipBounds* bounds, float zScale) {
    render::BeginFlatDrawTime(blocks * 4);
    int result = DrawFlat(this, positions, uvs, blocks, indices, indexCount, colour, specular, bounds, zScale);
    render::EndMesh();
    render::EndFlatDrawTime();
    return result;
}
