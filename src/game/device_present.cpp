// The device object's present, called once per frame by the frame function after the scene, the interface and the
// software cursor are drawn. The game flips the primary's chain in full screen and copies the render target to the
// primary at the client rectangle's corner in a window; here it ends the new renderer's frame, which shows it on the
// game window placed first (full screen over its monitor, present_window.h). See docs/notes/renderer.md.
#include "device.h"
#include "present_window.h"

#include "render/render.h"

// Returns the DirectDraw result: 0.
long __fastcall DevicePresent(CTextureDevice* device) {
    present_window::Update(device);
    render::Present();
    return 0;
}
