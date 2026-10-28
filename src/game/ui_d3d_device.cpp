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

} // namespace uidevice
