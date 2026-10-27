// Small device-state setters of the old renderer, written so the new renderer's state mirror
// (src/render/render_state.h) gets their values: the renderer's fog colour, its blend factors (the particles'
// additive / normal switch) and the device object's material. See docs/notes/renderer.md ("Pass state").
#include "app.h"
#include "device.h"
#include "device_material.h"
#include "model.h"   // CSceneRenderer

#include "ei/hook.h"
#include "lib/crt.h"
#include "render/render_state.h"

#include <cstdint>

// Sets FOGCOLOR; a failure is a fatal error (its result returned), else 1.
int CSceneRenderer::SetFogColourState(std::uint32_t colour) {
    IDirect3DDevice7* d3d = device::D3D(device_);
    long hr = device::SetRenderState(d3d, D3DRENDERSTATE_FOGCOLOR, colour);
    render::state::Render(D3DRENDERSTATE_FOGCOLOR, colour);
    if (hr != 0) return game::FatalError("Can't set fog color 0x%X.\n", colour);
    return 1;
}
EI_REWRITTEN(0x00501AB0, CSceneRenderer::SetFogColourState);

// Sets SRCBLEND, then DESTBLEND (D3DBLEND values: the particle draw passes 5, 2 for additive sources and 5, 6 to go
// back). A failure is reported (its result returned; DESTBLEND is then not set), else 1.
int CSceneRenderer::SetBlend(std::uint32_t source, std::uint32_t dest) {
    long hr = device::SetRenderState(device::D3D(device_), D3DRENDERSTATE_SRCBLEND, source);
    render::state::Render(D3DRENDERSTATE_SRCBLEND, source);
    if (hr != 0) return app::FatalErrorCode(hr, "Can't set source alpha blend mode.\n");
    hr = device::SetRenderState(device::D3D(device_), D3DRENDERSTATE_DESTBLEND, dest);
    render::state::Render(D3DRENDERSTATE_DESTBLEND, dest);
    if (hr != 0) return app::FatalErrorCode(hr, "Can't set destination alpha blend mode.\n");
    return 1;
}
EI_REWRITTEN(0x00501C60, CSceneRenderer::SetBlend);

void* CDeviceMaterial::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void CDeviceMaterial::operator delete(void* block) { gamecrt::operator_delete(block); }

// The current material (material_, counted; the old one released when its count reaches 0); without a material
// stage 1's colour and alpha operations are turned off. Returns 1.
int CTextureDevice::SetMaterial(CDeviceMaterial* material) {
    CDeviceMaterial* old = material_;
    material_ = material;
    if (material) ++material->refCount;
    if (old) old->Release();
    if (!material) {
        device::SetTextureStageState(device::D3D(this), 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device::SetTextureStageState(device::D3D(this), 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        render::state::Stage(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        render::state::Stage(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    }
    return 1;
}
EI_REWRITTEN(0x004EB170, CTextureDevice::SetMaterial);
