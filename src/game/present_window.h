#pragma once

// The game window the renderer presents on (render::StartPresenting): full screen without a
// display mode change, and the game's mouse coordinates when the frame does not fill the window one to one.
// docs/notes/renderer.md, section 9.
// More: docs/notes/comments-c22i.md, "present_window.h".

#include "lib/win_types.h"

class TextureDevice;

namespace present_window {

// Each present without DirectDraw: a full-screen device (flags bit 0) gets its window
// over the whole monitor it is on (not while minimised; again after a display mode change), and
// the cursor imports are mapped from the first call.
void Update(TextureDevice* device);

// A mouse message's coordinates (client coordinates; WM_MOUSEWHEEL screen coordinates) as frame
// pixels. Other messages, and everything outside the switch-over, unchanged.
LPARAM MouseToGame(UINT message, LPARAM lParam);

} // namespace present_window
