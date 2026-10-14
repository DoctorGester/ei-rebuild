// The renderer's rectangle (005041F0, virtual slot 19 of the renderer at 007AF060, vtable 0073D93C,
// RET 0x18): a black rectangle with the given alpha, filled as two pre-transformed triangles over
// the current blend state, and with `outline` a white one-pixel frame drawn as a line strip around
// it (the fill then stays a pixel inside). Used for the rubber-band selection (00636020) and the
// interface's fades (005E69C0). The new renderer (src/render) takes both draws; with "nointerface"
// the old path skips them. See docs/notes/renderer.md.
#include "ei/hook.h"
#include "device.h"
#include "model.h"
#include "texture.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "render/render_state.h"
#include "originals_render.h"

#include <cstdint>

namespace {

// The states this draw touches.
constexpr D3DRENDERSTATETYPE kShadeMode = D3DRENDERSTATE_SHADEMODE, kZBias = D3DRENDERSTATE_ZBIAS;
constexpr D3DTEXTURESTAGESTATETYPE kColourOp = D3DTSS_COLOROP, kAlphaOp = D3DTSS_ALPHAOP;
constexpr DWORD kOpDisable = D3DTOP_DISABLE, kOpModulate = D3DTOP_MODULATE;
constexpr DWORD kShadeFlat = D3DSHADE_FLAT, kShadeGouraud = D3DSHADE_GOURAUD;
constexpr DWORD kFvf = 0xC4;   // XYZRHW | DIFFUSE | SPECULAR

void Count(CTextureDevice* device, int vertices, int primitives) {
    device->vertexCount_ += vertices;
    device->primitiveCount_ += primitives;
    device->drawCount_ += 1;
}

} // namespace

// 005041F0. Returns 0 when the rectangle is inverted (right < left or bottom < top), else 1.
int __fastcall RendererRectangle(CSceneRenderer* renderer, void*, int left, int top, int right, int bottom, char alpha,
                                 char outline) {
    if (right < left || bottom < top) return 0;
    CTextureDevice* device = renderer->device_;
    std::uint32_t colour = static_cast<std::uint32_t>(static_cast<std::uint8_t>(alpha)) << 24;
    // With the outline the fill stays a pixel inside it.
    float inset = outline ? 1.0f : 0.0f;
    float x0 = static_cast<float>(left), y0 = static_cast<float>(top);
    float x1 = static_cast<float>(right), y1 = static_cast<float>(bottom);
    render::ScreenVertex v[5] = {
        {x0 + inset, y0 + inset, 0.0f, 1.0f, colour, 0xFF000000u},
        {x0 + inset, y1 - inset, 0.0f, 1.0f, colour, 0xFF000000u},
        {x1 - inset, y1 - inset, 0.0f, 1.0f, colour, 0xFF000000u},
        {x1 - inset, y0 + inset, 0.0f, 1.0f, colour, 0xFF000000u},
        {x0 + inset, y0 + inset, 0.0f, 1.0f, colour, 0xFF000000u},
    };
    const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

    if (device->stageSurfaces_[0]) {   // no texture on stage 0
        device->stageSurfaces_[0] = nullptr;
        DeviceFlushBatchAOriginal(device);   // 004E7840: draws the batched vertices
        device->lastLoadResult_ = device::SetTexture(device::D3D(device), 0, nullptr);
        ++device->textureChanges_;
    }
    device::SetRenderState(device::D3D(device), kZBias, 0);
    render::state::Render(kZBias, 0);
    if (render::ScreenDraw(device, v, 4, indices, 6)) {
        gameapi::EnterCriticalSection(device::Lock());
        device->lastLoadResult_ = device::DrawIndexedPrimitive(device::D3D(device), 
            D3DPT_TRIANGLELIST, kFvf, v, 4, const_cast<std::uint16_t*>(indices), 6, 0);
        gameapi::LeaveCriticalSection(device::Lock());
    }
    Count(device, 4, 2);
    if (!outline) return 1;

    // The frame: white, on the rectangle's own edges, flat shaded, with a depth bias, the texture
    // stage off.
    for (auto& p : v) p.colour = 0xFFFFFFFFu;
    v[0].x = x0, v[0].y = y0;
    v[1].x = x0, v[1].y = y1;
    v[2].x = x1, v[2].y = y1;
    v[3].x = x1, v[3].y = y0;
    v[4].x = x0, v[4].y = y0;
    device::SetRenderState(device::D3D(device), kShadeMode, kShadeFlat);
    render::state::Render(kShadeMode, kShadeFlat);
    device::SetRenderState(device::D3D(device), kZBias, 1);
    render::state::Render(kZBias, 1);
    device->lastLoadResult_ = device::SetTextureStageState(device::D3D(device), 0, kColourOp, kOpDisable);
    render::state::Stage(0, kColourOp, kOpDisable);
    device->lastLoadResult_ = device::SetTextureStageState(device::D3D(device), 0, kAlphaOp, kOpDisable);
    render::state::Stage(0, kAlphaOp, kOpDisable);
    if (render::ScreenDraw(device, v, 5, nullptr, 0)) {
        gameapi::EnterCriticalSection(device::Lock());
        device->lastLoadResult_ = device::DrawPrimitive(device::D3D(device), D3DPT_LINESTRIP, kFvf, v, 5, 0);
        gameapi::LeaveCriticalSection(device::Lock());
    }
    Count(device, 5, 1);
    device->lastLoadResult_ = device::SetTextureStageState(device::D3D(device), 0, kColourOp, kOpModulate);
    render::state::Stage(0, kColourOp, kOpModulate);
    device->lastLoadResult_ = device::SetTextureStageState(device::D3D(device), 0, kAlphaOp, kOpModulate);
    render::state::Stage(0, kAlphaOp, kOpModulate);
    device::SetRenderState(device::D3D(device), kShadeMode, kShadeGouraud);
    render::state::Render(kShadeMode, kShadeGouraud);
    device::SetRenderState(device::D3D(device), kZBias, 0);
    render::state::Render(kZBias, 0);
    return 1;
}
EI_REPLACE(0x005041F0, RendererRectangle);
