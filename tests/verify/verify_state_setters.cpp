// Checks for the small state setters rewritten for the state mirror (device_states.cpp,
// ui_alpha_draws.cpp). Ours only (they talk to Direct3D) except the blend setter 00501C60; the
// device's state is read back here to check what they leave.
#include "verify.h"

#include "../../src/game/device_material.h"
#include "../../src/game/model.h"
#include "../../src/game/texture.h"
#include "../../src/game/ui.h"

#include <cstdint>

namespace {
// The device's render state (without DirectDraw the switch-over device's, ui_d3d_device.cpp).
std::uint32_t RenderState(void* d3d, std::uint32_t state) {
    std::uint32_t value = 0;
    static_cast<UiD3DDevice*>(d3d)->GetRenderState(state, &value);
    return value;
}
void* WindowDevice(CUiWindow* w) { return w->interface_->renderer_->device_; }
} // namespace

int __fastcall RendererSetFogColour(CSceneRenderer* renderer, void*, std::uint32_t colour);
static void* s_fog;
static int __fastcall RendererSetFogColourVerify(CSceneRenderer* renderer, void*, std::uint32_t colour) {
    int result = RendererSetFogColour(renderer, nullptr, colour);
    void* d3d = renderer->device_->d3dDevice_;
    bool ok = result != 1 || RenderState(d3d, 0x22) == colour;
    ei::verify::Check("RendererSetFogColour", true, ok, "colour %08X", colour);
    return result;
}
EI_VERIFY(0x00501AB0, RendererSetFogColourVerify, 0, s_fog);

// 00501C60: differential on the real device (the particles' blend switch, scenarios particles /
// spells / effects): the original through the trampoline, the blend factors it leaves read back;
// then ours, read back again; results and factors compared. Both set the same two states, so the
// original's run has no effect of its own (a failing device would report the error twice).
int __fastcall RendererSetBlend(CSceneRenderer* renderer, void*, std::uint32_t source, std::uint32_t dest);
using SetBlendFn = int(__fastcall*)(CSceneRenderer*, void*, std::uint32_t, std::uint32_t);
static SetBlendFn s_blend;
static int __fastcall RendererSetBlendVerify(CSceneRenderer* renderer, void*, std::uint32_t source, std::uint32_t dest) {
    void* d3d = renderer->device_->d3dDevice_;
    int expected = s_blend(renderer, nullptr, source, dest);
    std::uint32_t expectedSource = RenderState(d3d, 0x13), expectedDest = RenderState(d3d, 0x14);
    int result = RendererSetBlend(renderer, nullptr, source, dest);
    std::uint32_t actualSource = RenderState(d3d, 0x13), actualDest = RenderState(d3d, 0x14);
    bool ok = result == expected && actualSource == expectedSource && actualDest == expectedDest &&
              (result != 1 || (actualSource == source && actualDest == dest));
    ei::verify::Check("RendererSetBlend", true, ok, "(%u, %u): result %d/%d, device %u, %u (original %u, %u)", source, dest,
                      result, expected, actualSource, actualDest, expectedSource, expectedDest);
    return result;
}
EI_VERIFY(0x00501C60, RendererSetBlendVerify, 7, s_blend);

static void* s_material;
static int __fastcall DeviceSetMaterialVerify(CTextureDevice* device, void*, CDeviceMaterial* material) {
    int result = device->SetMaterial(material);
    bool ok = result == 1 && device->material_ == material;
    ei::verify::Check("DeviceSetMaterial", true, ok, "material %p", material);
    return result;
}
EI_VERIFY(0x004EB170, DeviceSetMaterialVerify, 0, s_material);

void __fastcall WindowDrawChildren(CUiWindow* self, void*, int pass);
static void* s_children;
static void __fastcall WindowDrawChildrenVerify(CUiWindow* self, void*, int pass) {
    std::uint32_t before = RenderState(WindowDevice(self), 0x18);
    WindowDrawChildren(self, nullptr, pass);
    ei::verify::Check("WindowDrawChildren", true, RenderState(WindowDevice(self), 0x18) == before, "alpha reference not restored");
}
EI_VERIFY(0x00600F70, WindowDrawChildrenVerify, 0, s_children);

int __fastcall WindowDrawBlended(CUiWindow* self, void*, int pass);
static void* s_blended;
static int __fastcall WindowDrawBlendedVerify(CUiWindow* self, void*, int pass) {
    int result = WindowDrawBlended(self, nullptr, pass);
    ei::verify::Check("WindowDrawBlended", true, result == 1, "result %d", result);
    return result;
}
EI_VERIFY(0x0062EFC0, WindowDrawBlendedVerify, 0, s_blended);

int __fastcall WindowDrawAlphaRef(CUiWindow* self, void*, int pass);
static void* s_alphaRef;
static int __fastcall WindowDrawAlphaRefVerify(CUiWindow* self, void*, int pass) {
    std::uint32_t before = RenderState(WindowDevice(self), 0x18);
    int result = WindowDrawAlphaRef(self, nullptr, pass);
    bool ok = result == 1 && RenderState(WindowDevice(self), 0x18) == before;
    ei::verify::Check("WindowDrawAlphaRef", true, ok, "result %d", result);
    return result;
}
EI_VERIFY(0x0065A6D0, WindowDrawAlphaRefVerify, 0, s_alphaRef);
