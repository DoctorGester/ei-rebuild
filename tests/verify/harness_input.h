#pragma once

#include <windows.h>

#include <cstdio>

// The harness's mouse is the game's only mouse (verify build only; harness_input.cpp; ticket S21c1).
//
// A harness game reads the mouse three ways: window messages (WM_MOUSEMOVE and the buttons, through
// the input dispatcher 00457970), GetCursorPos (the cursor's position, 005E52E0) and the DirectInput
// mouse (CDirectInputMouse, made by 0065EAD0 non-exclusive in the background, polled by the cursor at
// 005E52E0 through 0065ECB0: the motion it sums at 007C7C60 turns the camera on a right-button drag).
// The harness answers GetCursorPos with its own mouse (harness.cpp HarnessCursorPos) and posts its
// own messages, but the user's real mouse still came in by the other two. The original's window under
// dgVoodoo covers the desktop (client area at 0,0, 3840x2160 here) and is not cloaked, so wherever the
// real cursor rests, the topmost game window gets WM_MOUSEMOVE with its coordinates: when the cursor
// moves, and when a starting game's window appears under it (seen: (0, 1823), the screen's left edge,
// which also scrolls the camera). A background DirectInput device reads the hardware whatever window is
// in front. Either one moved what the game saw under the cursor and the camera: the original's runs
// stopped repeating (S21c1: client units' model flags, the camera in the saves, then the particles'
// draws and with them every later random draw), depending on where the user's cursor was and which
// game's window was on top.
//
// So, in game.exe and in the standalone image alike (both create the device through the import slot
// 0073B044, DirectInputCreateEx):
// - the game's mouse device reads no motion and no buttons (IDirectInputDevice7A::GetDeviceState of
//   the device made by the game's CreateDeviceEx: the state is zeroed after the read);
// - the game window answers WM_NCHITTEST with HTNOWHERE, so the real cursor over it makes only
//   non-client mouse messages, which the game passes to DefWindowProc (posted messages, the harness's
//   input, never hit-test).
// What was left out is counted; the report's MOUSE: line says how often the real mouse moved.

namespace ei::verify::input {

// Routes DirectInputCreateEx (once; from the game's CreateWindowExA, harness.cpp CreateWithoutActivating: the
// DirectInput mouse takes the game window, so it comes later). Not at the harness's start: in the image the host
// still routes that slot itself then (standalone.cpp RouteImageCalls, which finds the slot by the export it holds,
// to give DirectInput the host's module handle), and a slot the harness had routed first was not found.
void Install();

// From the game window's subclass (harness.cpp AlwaysActiveWindowProc): true when the message was the
// real mouse's and is answered with *result.
bool RealMouse(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* result);

// The report's MOUSE: line.
void Report(FILE* f);

} // namespace ei::verify::input
