#pragma once

// EI_CAMERA_TRACE=1 traces the camera's mouse turn into ei-rebuild.log (docs/notes/ui.md, "Camera mouse turn"): a
// line a second with the frames (the cursor's polls of the mouse) and their longest and shortest intervals, and while
// the right button turns the camera a line a poll (its interval, the mouse's motion, the drag motion the turn takes
// from it, the camera's heading and pitch, the turn and tilt rates and targets) and a line a turning drag taken.

#include "lib/rect.h"

#include <cstdint>

namespace camera_trace {

bool On();
// The cursor polled the mouse at `now`, `interval` ms after its last poll: `motion` the device's, `drag` the turn's
// motion given from it (Cursor::TakeDrag), `held` while the camera turns.
void Poll(std::uint32_t now, std::uint32_t interval, Point motion, Point drag, bool held);
// The camera input took a turning drag: `drag` the motion the cursor gave, `layout` in layout units.
void Drag(Point drag, float layoutX, float layoutY);

// The global map took a turning drag: `drag` the counts, `layoutX` in layout units, `angle` the map's angle after it.
void MapDrag(Point drag, float layoutX, float angle);

} // namespace camera_trace
