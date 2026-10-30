#pragma once

// The system's cursor (docs/notes/platform.md, "Window and events"): its shape over the game window (the game hides
// it and draws its own) and its position, in the game's points.

#include "platform/window.h"

namespace platform {

// A native cursor shape (an HCURSOR on Windows); null shows none.
using CursorShape = void*;

CursorShape CurrentCursor();
void SetCursor(CursorShape shape);

// How the game's points relate to the screen's (the frame scaled into the window): `toGame` turns a screen point into
// the game's, `toScreen` the other way. Without (the start), the game's points are the screen's.
using PointMap = void (*)(Point* point);
void MapCursorPoints(PointMap toGame, PointMap toScreen);
// The cursor's position as a game point; false (and `at` unchanged) when the system could not tell.
bool CursorPosition(Point* at);
// Moves the cursor to the game point `at`.
void SetCursorPosition(Point at);

} // namespace platform
