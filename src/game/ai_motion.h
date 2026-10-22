#pragma once

// A unit's motion: the order its controller gives (MotionOrder, the header of AIPath; ai_map.h) and
// the trajectory the unit follows (UnitMotion, CUnit + 0x27C; unit_motion.h), sampled every tick
// for the unit's position, facing and speed. Names provisional.

#include "ai_map.h"
#include "unit_motion.h"

// The order (MotionOrder, the first 0x20 bytes of PathOrder) is in ai_map.h.
inline MotionOrder* OrderOf(PathOrder* path) { return &path->motionOrder; }
