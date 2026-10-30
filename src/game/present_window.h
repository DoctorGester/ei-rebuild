#pragma once

// The game window the renderer presents on (render::StartPresenting): full screen without a
// display mode change, and the game's mouse coordinates as frame pixels wherever the window is and whatever its size.
// docs/notes/renderer.md, section 9.
// More: docs/notes/comments-c22i.md, "present_window.h".

class TextureDevice;
namespace platform {
struct Event;
}

namespace present_window {

// Each present without DirectDraw: a full-screen device (flags bit 0) gets its window
// over the whole monitor it is on (not while minimised; again after a display mode change), and
// the cursor's points are mapped (platform::MapCursorPoints).
void Update(TextureDevice* device);

// A mouse event's position (client coordinates; the wheel's screen coordinates) as frame pixels. Other events
// unchanged.
void MouseToGame(platform::Event* event);

} // namespace present_window
