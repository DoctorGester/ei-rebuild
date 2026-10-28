// The old renderer's two line-strip draws, so the new renderer's state mirror (src/render/render_state.h) gets
// the states they set and the new renderer gets the lines (render::TlDraw; with the old path's meshes off the
// old draw is skipped).
// More: docs/notes/comments-c22i.md, "renderer_lines.cpp".
#include "renderer_lines.h"

#include "device.h"
#include "model.h"
#include "texture.h"
#include "game_singletons.h"

#include "lib/containers.h"
#include "lib/static_object.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "render/render_state.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// The states these draws touch.
constexpr D3DRENDERSTATETYPE kShadeMode = D3DRENDERSTATE_SHADEMODE;
constexpr DWORD kShadeFlat = D3DSHADE_FLAT, kShadeGouraud = D3DSHADE_GOURAUD;
constexpr D3DTEXTURESTAGESTATETYPE kColourOp = D3DTSS_COLOROP, kAlphaOp = D3DTSS_ALPHAOP;
constexpr DWORD kOpDisable = D3DTOP_DISABLE, kOpModulate = D3DTOP_MODULATE;
constexpr DWORD kFvf = 0xC4;   // XYZRHW | DIFFUSE | SPECULAR

// The draw itself, under the device lock, after the new renderer has taken it.
void DrawStrip(CTextureDevice* device, LineVertex* vertices, int count) {
    if (render::TlDraw(device, vertices, static_cast<int>(kFvf), count, nullptr, 0, D3DPT_LINESTRIP)) {
        gameapi::EnterCriticalSection(&g_deviceLock);
        device->lastLoadResult_ =
            device::DrawPrimitive(device::D3D(device), D3DPT_LINESTRIP, kFvf, vertices, static_cast<DWORD>(count), 0);
        gameapi::LeaveCriticalSection(&g_deviceLock);
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

void DestroyLineOutcodes() {   // (CByteArray's destructor)
    g_lineOutcodes.Destroy();
}

} // namespace

// Fewer than two points: 0. Else the strip in
// `colour` (opaque), the vertices in a temporary array; returns 1.
int CSceneRenderer::Polyline(const ScreenPoints* points, std::uint32_t colour) {
    int count = points->count;
    if (count < 2) return 0;
    std::vector<LineVertex> vertices(static_cast<std::size_t>(count));
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
    ::DrawStrip(device, vertices.data(), count);
    SetStageOps(device, kOpModulate);
    return 1;
}

// Flat shaded
// (Gouraud again after); returns the last SetRenderState's result.
long DeviceDrawLineStrip(CTextureDevice* device, const ProjectedPoints* points, std::uint32_t colour) {
    std::vector<LineVertex>& buffer = g_lineVertices;
    int count = points->count;
    buffer.resize(static_cast<std::size_t>(count));

    device::SetRenderState(device::D3D(device), kShadeMode, kShadeFlat);
    render::state::Render(kShadeMode, kShadeFlat);
    SetStageOps(device, kOpDisable);
    for (int i = 0; i < points->count; ++i) {
        LineVertex& v = buffer[i];
        std::memcpy(&v.x, &points->points[i], 16);
        v.colour = colour | 0xFF000000u;
        v.specular = 0xFF000000u;
    }
    DrawStrip(device, buffer.data(), static_cast<int>(buffer.size()));
    SetStageOps(device, kOpModulate);
    long hr = device::SetRenderState(device::D3D(device), kShadeMode, kShadeGouraud);
    render::state::Render(kShadeMode, kShadeGouraud);
    return hr;
}

// The line lists' work arrays (renderer_lines.h): zero at the start, as the static initialisers
// leave them; the exit functions they register free the blocks (docs/notes/standalone.md, "The static
// initialisers' objects"). 's array is never used: config/unread_data.csv.
constinit std::vector<LineVertex> g_lineVertices = /*007A9CE0*/ {};
constinit std::vector<ProjectedPoint> g_lineProjected = /*007A9CB8*/ {};
constinit lib::StaticObject<std::vector<std::uint8_t>> g_lineOutcodes = /*007A9CC8*/ {};

void FreeLineVertices() { lib::FreeStorage(g_lineVertices); }
void FreeLineProjected() { lib::FreeStorage(g_lineProjected); }
void ConstructLineOutcodes() {   // (CByteArray's constructor)
    g_lineOutcodes.Construct();
    std::atexit(DestroyLineOutcodes);
}
