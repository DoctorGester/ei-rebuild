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

// A mouse event's position (client coordinates; the wheel's screen coordinates) as frame pixels, a point beyond the
// frame (over the bars of a window of another shape) put on its nearest pixel. Returns whether the point was on the
// frame; other events are unchanged (true).
bool MouseToGame(platform::Event* event);

} // namespace present_window
