// The renderer's rectangle draw: a black rectangle with the given alpha, filled as two
// pre-transformed triangles over the current blend state, and with `outline` a white one-pixel frame drawn as
// a line strip around it (the fill then stays a pixel inside). Used for the rubber-band selection and the
// interface's fades. The new renderer (src/render) takes both draws. See docs/notes/renderer.md.
#include "model.h"
#include "texture.h"
#include "render/render.h"
#include "render/render_state.h"

#include <cstdint>

namespace {

void SetStage0Ops(render::TextureOp op) {
    render::g_renderState.stages[0].combiner.colourOp = op;
    render::g_renderState.stages[0].combiner.alphaOp = op;
}

} // namespace

// Returns 0 when the rectangle is inverted (right < left or bottom < top), else 1.
int CSceneRenderer::DrawRectangle(int left, int top, int right, int bottom, int alphaArgument, int outlineArgument) {
    const char alpha = static_cast<char>(alphaArgument);   // the low bytes count
    const char outline = static_cast<char>(outlineArgument);
    if (right < left || bottom < top) return 0;
    CTextureDevice* currentDevice = device;
    std::uint32_t packed = static_cast<std::uint32_t>(static_cast<std::uint8_t>(alpha)) << 24;
    // With the outline the fill stays a pixel inside it.
    float inset = outline ? 1.0f : 0.0f;
    float x0 = static_cast<float>(left), y0 = static_cast<float>(top);
    float x1 = static_cast<float>(right), y1 = static_cast<float>(bottom);
    render::ScreenVertex v[5] = {
        {x0 + inset, y0 + inset, 0.0f, 1.0f, packed, 0xFF000000u},
        {x0 + inset, y1 - inset, 0.0f, 1.0f, packed, 0xFF000000u},
        {x1 - inset, y1 - inset, 0.0f, 1.0f, packed, 0xFF000000u},
        {x1 - inset, y0 + inset, 0.0f, 1.0f, packed, 0xFF000000u},
        {x0 + inset, y0 + inset, 0.0f, 1.0f, packed, 0xFF000000u},
    };
    const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

    currentDevice->stageSurfaces[0] = nullptr;    // no texture on stage 0
    render::ScreenDraw(currentDevice, v, 4, indices, 6);
    if (!outline) return 1;

    // The frame: white, on the rectangle's own edges, the texture stage off.
    for (auto& p : v) p.colour = 0xFFFFFFFFu;
    v[0].x = x0, v[0].y = y0;
    v[1].x = x0, v[1].y = y1;
    v[2].x = x1, v[2].y = y1;
    v[3].x = x1, v[3].y = y0;
    v[4].x = x0, v[4].y = y0;
    SetStage0Ops(render::TextureOp::Disable);
    render::ScreenDraw(currentDevice, v, 5, nullptr, 0);
    SetStage0Ops(render::TextureOp::Modulate);
    return 1;
}
