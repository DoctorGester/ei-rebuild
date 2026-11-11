#pragma once

// The game window the renderer presents on (render::StartPresenting): full screen without a
// display mode change, and the game's mouse coordinates as frame pixels wherever the window is and whatever its size.
// docs/notes/renderer.md, section 9.
// More: docs/notes/platform.md, "Window and events".

class TextureDevice;
namespace platform {
struct Event;
}

namespace present_window {

// Each present without DirectDraw: a full-screen device (flags bit 0) gets its window
// over the whole monitor it is on (not while minimised; again after a display mode change), and
// the cursor's points are mapped (platform::MapCursorPoints). A window that is not full screen and whose client
// area took another size since the last present (dragged, maximised, a DPI change) asks for that size as the
// rendering resolution (app::RequestResolution); the frame is scaled into the window until the next pass takes it.
void Update(TextureDevice* device);

// The device's present, once a frame after the scene, the interface and the software cursor are drawn (and once a
// movie frame): Update, then the renderer's frame shown on the window (render::Present). See docs/notes/renderer.md.
void Present(TextureDevice* device);

// A window that is not full screen: its client area sized to `width` x `height` (platform::SetClientSize: made smaller
// when it would not fit, the frame then scaled into it), taken as the window's own size, so the next present asks for
// no resolution.
void SizeClient(TextureDevice* device, int width, int height);

// The window switched to full screen (covering its monitor from the next present) or to a window whose client area
// is the screen's size (SizeClient); the device's flag follows.
void SetFullScreen(TextureDevice* device, bool fullScreen);

// A mouse event's position (in the window) as frame pixels, a point beyond the frame (over the bars of a window of
// another shape) put on its nearest pixel. Returns whether the point was on the frame; other events are unchanged
// (true).
bool MouseToGame(platform::Event* event);

} // namespace present_window
