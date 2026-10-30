// The old renderer's line-strip draw: stage 0 off around it (src/render/render_state.h), the lines to the new
// renderer (render::TlDraw).
// More: docs/notes/comments-c22i.md, "renderer_lines.cpp".
#include "renderer_lines.h"

#include "figure_mesh.h"   // VertexPacket
#include "model.h"
#include "texture.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "render/render.h"
#include "render/render_state.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// The draw itself, to the new renderer.
void DrawStrip(TextureDevice* device, LineVertex* vertices, int count) {
    render::TlDraw(device, vertices, render::kFvfUntextured, count, nullptr, 0, render::Primitive::LineStrip);
}

void SetStage0Ops(render::TextureOp op) {
    render::g_renderState.stages[0].combiner.colourOp = op;
    render::g_renderState.stages[0].combiner.alphaOp = op;
}

} // namespace

// In one colour (the frame's alpha opaque), stage 0 off and back to modulate. Returns 0.
long DeviceDrawLineStrip(TextureDevice* device, const ProjectedPoints* points, std::uint32_t colour) {
    std::vector<LineVertex>& buffer = g_lineVertices;
    int count = points->count;
    buffer.resize(static_cast<std::size_t>(count));

    SetStage0Ops(render::TextureOp::Disable);
    for (int i = 0; i < points->count; ++i) {
        LineVertex& v = buffer[i];
        std::memcpy(&v.x, &points->points[i], 16);
        v.colour = colour | 0xFF000000u;
        v.specular = 0xFF000000u;
    }
    DrawStrip(device, buffer.data(), static_cast<int>(buffer.size()));
    SetStage0Ops(render::TextureOp::Modulate);
    return 0;
}

// The line lists' work arrays (renderer_lines.h).
std::vector<LineVertex> g_lineVertices;
std::vector<Vector4> g_lineProjected;
std::vector<std::uint8_t> g_lineOutcodes;

// --- Line batch --------------------------------------------------------------------------------------

// The renderer's line lists (renderer_lines.h): one function appends a point, another projects a list and
// hands the strip to the device object's line draw (renderer_lines.cpp). Debug drawing: the figure parts'
// bounds boxes and the world client's line lists. See docs/notes/renderer.md.

int LineBatch::AddPoint(const float* point) {
    if (count == std::ssize(packets) * 4) packets.resize(std::ssize(packets) + 1);
    auto put = [this, point](int index) {
        VertexPacket& packet = packets[index >> 2];
        int slot = index & 3;
        packet.x()[slot] = point[0];
        packet.y()[slot] = point[1];
        packet.z()[slot] = point[2];
        packet.w()[slot] = 1.0f;
    };
    put(count);
    ++count;
    for (int index = count; index < std::ssize(packets) * 4; ++index) put(index);
    return count;
}

int LineBatch::Draw(TransformPipeline* pipeline, TextureDevice* device) {
    std::vector<Vector4>& projected = g_lineProjected;
    projected.resize(std::ssize(packets) * 4);
    g_lineOutcodes.resize(std::ssize(packets) * 4);
    int visibility = pipeline->ProjectClipped(*pipeline->Top(TransformPipeline::kClipStack), packets.data(),
                                              projected.data(), g_lineOutcodes.data(), lib::Count(packets));
    if (visibility == 0) {
        const ProjectedPoints points{projected.data(), static_cast<int>(lib::Count(projected))};
        DeviceDrawLineStrip(device, &points, colour);
    }
    return 1;
}
