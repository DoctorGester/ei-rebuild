// The interface code's view of the IDirect3DDevice7 (UiD3DDevice, ui.h): each method goes through
// device.h's call of the same name, which uses the interface, or without DirectDraw the switch-over
// device's state (ticket R9b, docs/notes/renderer.md section 9).
#include "ui.h"

#include "device.h"

namespace {
IDirect3DDevice7* Interface(UiD3DDevice* device) { return reinterpret_cast<IDirect3DDevice7*>(device); }
} // namespace

long UiD3DDevice::Clear(DWORD count, void* rects, DWORD flags, DWORD colour, float z, DWORD stencil) {
    return device::Clear(Interface(this), count, static_cast<D3DRECT*>(rects), flags, colour, z, stencil);
}

long UiD3DDevice::GetTextureStageState(DWORD stage, DWORD type, std::uint32_t* value) {
    return device::GetTextureStageState(Interface(this), stage, static_cast<D3DTEXTURESTAGESTATETYPE>(type),
                                        reinterpret_cast<DWORD*>(value));
}

long UiD3DDevice::SetTextureStageState(DWORD stage, DWORD type, DWORD value) {
    return device::SetTextureStageState(Interface(this), stage, static_cast<D3DTEXTURESTAGESTATETYPE>(type), value);
}

long UiD3DDevice::SetRenderState(std::uint32_t state, std::uint32_t value) {
    return device::SetRenderState(Interface(this), static_cast<D3DRENDERSTATETYPE>(state), value);
}

long UiD3DDevice::GetRenderState(std::uint32_t state, std::uint32_t* value) {
    return device::GetRenderState(Interface(this), static_cast<D3DRENDERSTATETYPE>(state),
                                  reinterpret_cast<DWORD*>(value));
}

long UiD3DDevice::ApplyStateBlock(std::uint32_t handle) { return device::ApplyStateBlock(Interface(this), handle); }
