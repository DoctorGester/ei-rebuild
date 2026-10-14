#pragma once

// The game window in the switch-over without DirectDraw (render::PresentsItself, ticket R9c): full
// screen without a display mode change, and the game's mouse coordinates when the frame does not
// fill the window one to one. docs/notes/renderer.md, section 9.
//
// The game takes its mouse positions as screen coordinates of a window at 0, 0 the size of its
// resolution: GetCursorPos (the cursor 005E52E0, the global map 0062F050) and SetCursorPos without
// converting, WM_MOUSEMOVE and the buttons as client coordinates, WM_MOUSEWHEEL as screen
// coordinates. With the frame scaled into a larger window (render::FrameInWindow) these are mapped
// to frame pixels: the game's two imports by replacing their slots, the messages in the input
// dispatcher (00457970, app.cpp). Outside the switch-over, and when the frame fills the window at
// 0, 0 one to one, nothing changes.

#include <windows.h>

class CTextureDevice;

namespace present_window {

// Each present (004EF610) without DirectDraw: a full-screen device (flags_ bit 0) gets its window
// over the whole monitor it is on (not while minimised; again after a display mode change), and
// the cursor imports are mapped from the first call.
void Update(CTextureDevice* device);

// A mouse message's coordinates (client coordinates; WM_MOUSEWHEEL screen coordinates) as frame
// pixels. Other messages, and everything outside the switch-over, unchanged.
LPARAM MouseToGame(UINT message, LPARAM lParam);

} // namespace present_window
