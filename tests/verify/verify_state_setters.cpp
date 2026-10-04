// Checks for the small state setters rewritten for the state mirror (device_states.cpp,
// ui_alpha_draws.cpp). All ours only (they talk to Direct3D); the device's state is read back here
// to check what they leave.
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstdint>

namespace {
std::uint32_t RenderState(void* d3d, std::uint32_t state) {
    std::uint32_t value = 0;
    using Fn = long(__stdcall*)(void*, std::uint32_t, std::uint32_t*);
    reinterpret_cast<Fn>((*static_cast<void***>(d3d))[21])(d3d, state, &value);
    return value;
}
void* WindowDevice(CUiWindow* w) {
    return *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(w->interface_->renderer_) + 0x54);
}
} // namespace

int __fastcall RendererSetFogColour(char* renderer, void*, std::uint32_t colour);
static void* s_fog;
static int __fastcall RendererSetFogColourVerify(char* renderer, void*, std::uint32_t colour) {
    int result = RendererSetFogColour(renderer, nullptr, colour);
    void* d3d = *reinterpret_cast<void**>(*reinterpret_cast<char**>(renderer + 0x20) + 0x54);
    bool ok = result != 1 || RenderState(d3d, 0x22) == colour;
    ei::verify::Check("RendererSetFogColour", true, ok, "colour %08X", colour);
    return result;
}
EI_VERIFY(0x00501AB0, RendererSetFogColourVerify, 0, s_fog);

int __fastcall DeviceSetMaterial(char* device, void*, void* material);
static void* s_material;
static int __fastcall DeviceSetMaterialVerify(char* device, void*, void* material) {
    int result = DeviceSetMaterial(device, nullptr, material);
    bool ok = result == 1 && *reinterpret_cast<void**>(device + 0x14C) == material;
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
