// The old renderer's line-strip draw: stage 0 off around it (src/render/render_state.h), the lines to the new
// renderer (render::TlDraw).
// More: docs/notes/comments-c22i.md, "renderer_lines.cpp".
#include "renderer_lines.h"

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
void DrawStrip(CTextureDevice* device, LineVertex* vertices, int count) {
    render::TlDraw(device, vertices, render::kFvfUntextured, count, nullptr, 0, render::Primitive::LineStrip);
}

void SetStage0Ops(render::TextureOp op) {
    render::g_renderState.stages[0].combiner.colourOp = op;
    render::g_renderState.stages[0].combiner.alphaOp = op;
}


} // namespace

// In one colour (the frame's alpha opaque), stage 0 off and back to modulate. Returns 0.
long DeviceDrawLineStrip(CTextureDevice* device, const ProjectedPoints* points, std::uint32_t colour) {
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
std::vector<ProjectedPoint> g_lineProjected;
std::vector<std::uint8_t> g_lineOutcodes;
