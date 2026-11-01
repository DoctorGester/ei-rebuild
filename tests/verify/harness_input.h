#pragma once

#include <windows.h>

#include <cstdio>

// The harness's input is the game's only input (harness_input.cpp).
//
// A harness game reads the keyboard and the mouse as the platform layer's events (keys, characters, the mouse's
// moves and buttons, the wheel), the mouse's position (CursorPosition: the cursor, edge scrolling) and the mouse's
// motion (MouseDevice: the camera on a right-button drag), all from the harness's own platform backend
// (harness_platform.h): its queue of the harness's events, its motion count, its position from the harness's mouse
// (harness.cpp HarnessCursorPos). The real keyboard and mouse never reach the game: the backend drops every system
// event (S21c1: the real mouse made the original's runs stop repeating). The steps still speak
// window messages (WM_KEYDOWN, WM_LBUTTONDOWN, ...): Post makes each into the event the window procedure's
// translation made of it.

namespace ei::verify::input {

// The window message `message` (wParam, lParam as the steps post them) as the game's event, queued for its loops;
// WM_ACTIVATE tells the game at once (the focus steps, as SendMessage did).
void Post(UINT message, WPARAM wParam, LPARAM lParam);

// The harness's own motion (the "mousedrag" step): the game's next read of its mouse reports (dx, dy) more.
void AddMotion(int dx, int dy);


// The report's MOUSE: line.
void Report(FILE* f);

} // namespace ei::verify::input
