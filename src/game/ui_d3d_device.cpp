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

bool HasStencil(const CTextureDevice* device) {
    constexpr std::uint32_t kStencilFormat = 0x4000;   // DDPF_STENCILBUFFER
    return (device->depthFormat_.words[1] & kStencilFormat) != 0 && device->depthFormat_.words[4] != 0;
}

} // namespace uidevice
