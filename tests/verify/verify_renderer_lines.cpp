// Checks for the old renderer's line-strip draws (renderer_lines.cpp). Ours only: they draw with
// Direct3D, and the original would draw the strip a second time. What they leave is checked: the
// result, the device object's counters (one draw call, the vertices, a third of them as
// primitives), stage 0's colour and alpha operations back at MODULATE (read from the device), and
// for 0049B4B0 the shade mode back at GOURAUD and its vertex buffer (007A9CE0) holding the points
// with the opaque colour. Neither is reached by the scenarios (005045D0 has no caller found;
// 0049B4B0 draws the debug line lists).
#include "verify.h"

#include "../../src/game/device.h"
#include "../../src/game/model.h"
#include "../../src/game/renderer_lines.h"
#include "../../src/game/texture.h"

#include <cstdint>
#include <cstring>

namespace {

struct Counters {
    int vertices, primitives, calls;
};
Counters Read(const CTextureDevice* device) {
    return {device->vertexCount_, static_cast<int>(device->primitiveCount_), device->drawCount_};
}
bool Counted(const Counters& before, const Counters& after, int count) {
    return after.calls == before.calls + 1 && after.vertices == before.vertices + count &&
           static_cast<unsigned>(after.primitives - before.primitives) == static_cast<unsigned>(count) / 3;
}

std::uint32_t RenderState(IDirect3DDevice7* d3d, D3DRENDERSTATETYPE state) {
    DWORD value = 0;
    device::GetRenderState(d3d, state, &value);
    return value;
}
std::uint32_t StageState(IDirect3DDevice7* d3d, DWORD stage, D3DTEXTURESTAGESTATETYPE state) {
    DWORD value = 0;
    device::GetTextureStageState(d3d, stage, state, &value);
    return value;
}
bool StageModulates(IDirect3DDevice7* d3d) {
    return StageState(d3d, 0, D3DTSS_COLOROP) == D3DTOP_MODULATE && StageState(d3d, 0, D3DTSS_ALPHAOP) == D3DTOP_MODULATE;
}

} // namespace

// 005045D0.
static void* s_polyline;
static int __fastcall RendererPolylineVerify(CSceneRenderer* renderer, void*, const ScreenPoints* points,
                                             std::uint32_t colour) {
    CTextureDevice* device = renderer->device_;
    Counters before = Read(device);
    int count = points->count;
    int result = RendererPolyline(renderer, nullptr, points, colour);
    Counters after = Read(device);
    bool ok = count < 2 ? result == 0 && after.calls == before.calls && after.vertices == before.vertices
                        : result == 1 && Counted(before, after, count) && StageModulates(device::D3D(device));
    ei::verify::Check("RendererPolyline", true, ok, "%d points: result %d, draws %d, vertices %d", count, result,
                      after.calls - before.calls, after.vertices - before.vertices);
    return result;
}
EI_VERIFY(0x005045D0, RendererPolylineVerify, 0, s_polyline);

// 0049B4B0.
static void* s_lineStrip;
static long __fastcall DeviceDrawLineStripVerify(CTextureDevice* device, const ProjectedPoints* points, std::uint32_t colour) {
    Counters before = Read(device);
    int count = points->count;
    long result = DeviceDrawLineStrip(device, points, colour);
    Counters after = Read(device);
    const LineBuffer& buffer = LineVertices();
    bool vertices = buffer.data && buffer.size == count && buffer.capacity >= count;
    for (int i = 0; vertices && i < count; ++i) {
        const LineVertex& v = buffer.data[i];
        vertices = std::memcmp(&v.x, points->xyzw[i], 16) == 0 && v.colour == (colour | 0xFF000000u) && v.specular == 0xFF000000u;
    }
    IDirect3DDevice7* d3d = device::D3D(device);
    bool ok = vertices && Counted(before, after, count) && StageModulates(d3d) &&
              RenderState(d3d, D3DRENDERSTATE_SHADEMODE) == D3DSHADE_GOURAUD;
    ei::verify::Check("DeviceDrawLineStrip", true, ok, "%d points: result %ld, draws %d, buffer %d/%d, vertices %s", count,
                      result, after.calls - before.calls, buffer.size, buffer.capacity, vertices ? "ok" : "differ");
    return result;
}
EI_VERIFY(0x0049B4B0, DeviceDrawLineStripVerify, 0, s_lineStrip);
