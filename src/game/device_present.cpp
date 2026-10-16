// The device object's present (004EF610, __fastcall on the device object 007AF080; called once per
// frame by the frame function 00458010 after the scene, the interface and the software cursor are
// drawn). The original flips the primary's chain in full screen and copies the render target to the
// primary at the client rectangle's corner in a window; ours ends the new renderer's frame, which
// shows it on the game window placed first (full screen over its monitor, present_window.h). See
// docs/notes/renderer.md.
#include "device.h"
#include "present_window.h"

#include "ei/hook.h"
#include "render/render.h"

// 004EF610. Returns the DirectDraw result: 0.
long __fastcall DevicePresent(CTextureDevice* device) {
    present_window::Update(device);
    render::Present();
    return 0;
}
EI_REPLACE(0x004EF610, DevicePresent);
