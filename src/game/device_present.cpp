// The device object's present (004EF610, __fastcall on the device object 007AF080; called once per
// frame by the frame function 00458010 after the scene, the interface and the software cursor are
// drawn): full screen flips the primary's chain, a window copies the render target to the
// primary at the client rectangle's corner. The new renderer (src/render) ends its frame here.
// See docs/notes/renderer.md.
#include "device.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"

// 004EF610. Returns the DirectDraw result (0 when there is nothing to present).
long __fastcall DevicePresent(CTextureDevice* device) {
    // With "present" the new renderer copies its frame into the render target here, which the old
    // present then shows; without DirectDraw (device.h) it has shown the frame itself.
    render::Present();
    if (device::WithoutDirectDraw()) return 0;
    IDirectDrawSurface7* primary = device::Surface(device->primary_);
    CDXSurface* target = device->renderTarget_;
    IDirectDrawSurface7* targetSurface = device::Surface(target);
    if (primary == targetSurface) return 0;
    if (device->flags_ & 1) return primary->Flip(targetSurface, 0);
    // A window: the render target's own device object's primary.
    IDirectDrawSurface7* screen = device::Surface(target->device->primary_);
    if (screen == targetSurface) return 0;
    DWORD x = static_cast<DWORD>(device->screen_.left), y = static_cast<DWORD>(device->screen_.top);
    gameapi::EnterCriticalSection(device::Lock());
    long result = screen->BltFast(x, y, targetSurface, nullptr, DDBLTFAST_WAIT);
    gameapi::LeaveCriticalSection(device::Lock());
    return result;
}
EI_REPLACE(0x004EF610, DevicePresent);
