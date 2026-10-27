// The interface code's calls on the device's IDirect3DDevice7 (uidevice, ui.h): each goes through
// device.h's call of the same name, which uses the interface, or without DirectDraw the switch-over
// device's state (docs/notes/renderer.md section 9).
#include "ui.h"

#include "device.h"
#include "texture.h"

namespace uidevice {

long Clear(CTextureDevice* device, DWORD count, void* rects, DWORD flags, DWORD colour, float z, DWORD stencil) {
    return device::Clear(device->d3dDevice_, count, static_cast<D3DRECT*>(rects), flags, colour, z, stencil);
}

long GetTextureStageState(CTextureDevice* device, DWORD stage, DWORD type, std::uint32_t* value) {
    return device::GetTextureStageState(device->d3dDevice_, stage, static_cast<D3DTEXTURESTAGESTATETYPE>(type),
                                        reinterpret_cast<DWORD*>(value));
}

long SetTextureStageState(CTextureDevice* device, DWORD stage, DWORD type, DWORD value) {
    return device::SetTextureStageState(device->d3dDevice_, stage, static_cast<D3DTEXTURESTAGESTATETYPE>(type), value);
}

long SetRenderState(CTextureDevice* device, std::uint32_t state, std::uint32_t value) {
    return device::SetRenderState(device->d3dDevice_, static_cast<D3DRENDERSTATETYPE>(state), value);
}

long GetRenderState(CTextureDevice* device, std::uint32_t state, std::uint32_t* value) {
    return device::GetRenderState(device->d3dDevice_, static_cast<D3DRENDERSTATETYPE>(state),
                                  reinterpret_cast<DWORD*>(value));
}

long ApplyStateBlock(CTextureDevice* device, std::uint32_t handle) {
    return device::ApplyStateBlock(device->d3dDevice_, handle);
}

bool HasStencil(const CTextureDevice* device) {
    constexpr std::uint32_t kStencilFormat = 0x4000;   // DDPF_STENCILBUFFER
    return (device->depthFormat_.words[1] & kStencilFormat) != 0 && device->depthFormat_.words[4] != 0;
}

} // namespace uidevice
