// The old renderer's two line-strip draws, rewritten so the new renderer's state mirror
// (src/render/render_state.h) gets the states they set and the new renderer gets the lines
// (render::TlDraw; with the old path's meshes off the old draw is skipped). Both draw untextured
// one-colour strips: stage 0's colour and alpha operations off around the draw, back to MODULATE
// after it. See docs/notes/renderer.md ("Pass state").
//
// - 005045D0 (virtual slot 20 of the renderer 007AF060, vtable 0073D93C): a strip through integer
//   screen points. No caller found in the exe (not reached by the scenarios).
// - 0049B4B0 (the device object): a strip through points the transform pipeline projected; the
//   draw of the renderer's line lists (0049B780, slot 15 00501CE0: the figure parts' debug bounds
//   boxes of 005A7290, the world client's line lists in 00569D60). Debug drawing; not reached by
//   the scenarios. Its vertices go to a growing buffer in the exe's data (007A9CE0).
#include "renderer_lines.h"

#include "device.h"
#include "model.h"
#include "texture.h"
#include "game_singletons.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "render/render_state.h"

#include <cstdint>
#include <cstring>

namespace {

// The states these draws touch.
constexpr D3DRENDERSTATETYPE kShadeMode = D3DRENDERSTATE_SHADEMODE;
constexpr DWORD kShadeFlat = D3DSHADE_FLAT, kShadeGouraud = D3DSHADE_GOURAUD;
constexpr D3DTEXTURESTAGESTATETYPE kColourOp = D3DTSS_COLOROP, kAlphaOp = D3DTSS_ALPHAOP;
constexpr DWORD kOpDisable = D3DTOP_DISABLE, kOpModulate = D3DTOP_MODULATE;
constexpr DWORD kFvf = 0xC4;   // XYZRHW | DIFFUSE | SPECULAR

// 0049BB10 inlined: `count` vertices from the game's heap, 32-byte aligned, the block's own
// address in the dword before them (freed through it).
LineVertex* NewVertices(int count) {
    auto* block = static_cast<std::uint8_t*>(gamecrt::operator_new(static_cast<std::size_t>(count * 0x18 + 0x24)));
    auto* data = reinterpret_cast<LineVertex*>((reinterpret_cast<std::uintptr_t>(block) + 0x23) & ~std::uintptr_t{0x1F});
    reinterpret_cast<void**>(data)[-1] = block;
    return data;
}
void DeleteVertices(LineVertex* data) { gamecrt::operator_delete(reinterpret_cast<void**>(data)[-1]); }

// The draw itself, under the device lock, after the new renderer has taken it.
void DrawStrip(CTextureDevice* device, LineVertex* vertices, int count) {
    if (render::TlDraw(device, vertices, static_cast<int>(kFvf), count, nullptr, 0, D3DPT_LINESTRIP)) {
        gameapi::EnterCriticalSection(TheDeviceLock());
        device->lastLoadResult_ =
            device::DrawPrimitive(device::D3D(device), D3DPT_LINESTRIP, kFvf, vertices, static_cast<DWORD>(count), 0);
        gameapi::LeaveCriticalSection(TheDeviceLock());
    }
    device->vertexCount_ += count;
    device->primitiveCount_ += static_cast<unsigned>(count) / 3;
    device->drawCount_ += 1;
}

void SetStageOps(CTextureDevice* device, DWORD op) {
    device->lastLoadResult_ = device::SetTextureStageState(device::D3D(device), 0, kColourOp, op);
    render::state::Stage(0, kColourOp, op);
    device->lastLoadResult_ = device::SetTextureStageState(device::D3D(device), 0, kAlphaOp, op);
    render::state::Stage(0, kAlphaOp, op);
}

} // namespace

// 005045D0 (thiscall on the renderer, RET 8). Fewer than two points: 0. Else the strip in
// `colour` (opaque), the vertices in a temporary array; returns 1.
int CSceneRenderer::PolylineImpl(const ScreenPoints* points, std::uint32_t colour) {
    int count = points->count;
    if (count < 2) return 0;
    LineVertex* vertices = NewVertices(count);
    for (int i = 0; i < points->count; ++i) {
        LineVertex& v = vertices[i];
        v.x = static_cast<float>(points->xy[i * 2]);
        v.y = static_cast<float>(points->xy[i * 2 + 1]);
        v.z = 0.0f;
        v.rhw = 1.0f;
        v.colour = colour | 0xFF000000u;
        v.specular = 0xFF000000u;
    }
    CTextureDevice* device = device_;
    SetStageOps(device, kOpDisable);
    ::DrawStrip(device, vertices, count);
    SetStageOps(device, kOpModulate);
    DeleteVertices(vertices);
    return 1;
}
EI_REPLACE_METHOD(0x005045D0, CSceneRenderer::PolylineImpl);

// 0049B4B0 (fastcall: the device object, the points; `colour` on the stack, RET 4). Flat shaded
// (Gouraud again after); returns the last SetRenderState's result.
long __fastcall DeviceDrawLineStrip(CTextureDevice* device, const ProjectedPoints* points, std::uint32_t colour) {
    LineBuffer& buffer = LineVertices();
    int count = points->count;
    if (!buffer.data) {
        buffer.data = NewVertices(count);
        buffer.capacity = count;
        buffer.size = count;
    } else if (count <= buffer.capacity) {
        buffer.size = count;
    } else {
        int grow = buffer.growBy;
        if (grow == 0) {
            grow = buffer.size / 8;
            if (grow < 4) grow = 4;
            else if (grow > 1024) grow = 1024;
        }
        int capacity = buffer.capacity + grow;
        if (count >= capacity) capacity = count;
        LineVertex* data = NewVertices(capacity);
        DeleteVertices(buffer.data);
        buffer.capacity = capacity;
        buffer.data = data;
        buffer.size = count;
    }

    device::SetRenderState(device::D3D(device), kShadeMode, kShadeFlat);
    render::state::Render(kShadeMode, kShadeFlat);
    SetStageOps(device, kOpDisable);
    for (int i = 0; i < points->count; ++i) {
        LineVertex& v = buffer.data[i];
        std::memcpy(&v.x, points->xyzw[i], 16);
        v.colour = colour | 0xFF000000u;
        v.specular = 0xFF000000u;
    }
    DrawStrip(device, buffer.data, buffer.size);
    SetStageOps(device, kOpModulate);
    long hr = device::SetRenderState(device::D3D(device), kShadeMode, kShadeGouraud);
    render::state::Render(kShadeMode, kShadeGouraud);
    return hr;
}
EI_REPLACE(0x0049B4B0, DeviceDrawLineStrip);

// The line lists' work arrays (renderer_lines.h): zero at the start, as the static initialisers 0049B460 and
// 0049B6E0 leave them; the exit functions they register free the blocks (docs/notes/standalone.md, "The static
// initialisers' objects"). 0049B730's array (007A9CA8) is never used: config/unread_data.csv.
constinit LineBuffer g_lineVertices = /*007A9CE0*/ {};
constinit lib::AlignedArray<ProjectedPoint> g_lineProjected = /*007A9CB8*/ {};

namespace {
void __cdecl FreeLineVertices() {   // 0049B490
    if (g_lineVertices.data) lib::FreeAligned32(g_lineVertices.data);
}
void __cdecl FreeLineProjected() { g_lineProjected.FreeData(); }   // 0049B710
} // namespace

EI_REPLACE(0x0049B490, FreeLineVertices);
EI_REPLACE(0x0049B710, FreeLineProjected);
