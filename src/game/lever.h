#pragma once

#include "maths.h"

// The spots a use order may go to (an MFC CArray<Vector3>, vtable 0073C120; its SetAtGrow is
// 0052CC50). combat_planning.cpp's SpotArray is the same array.
struct LeverSpots {
    void* vtable;
    Vector3* data;      // +0x04
    int size;           // +0x08
    int capacity;       // +0x0C
    int growBy;         // +0x10
};
static_assert(sizeof(LeverSpots) == 0x14);

// Levers (units with positions; script.md "levers"): +0x1C the position, +0x34 the orientation
// (a quaternion), +0xA4 the model name, +0xC8 the shown position (0..1), +0xCC a second
// animation value that scales the spots' distance, +0x134 the radius, +0x238 the state, +0x23C
// the number of states, +0x240 cycling (wraps instead of stopping at the last state), +0x250 /
// +0x254 the animation's start and end position, +0x258 the end tick, +0x25C 1 / duration, +0x261
// the AI map flag kept while the lever moves. Only the members used by the rewrite are known
// (unit_lever.cpp).
struct Lever {
    // 0052B7A0 (RET 8). Sets state `state` (negative: the next one, wrapping when cycling) and
    // moves the lever there over `duration` ticks (negative: the model's default from 0079C1AC,
    // else 10; zero or less: at once). The units standing around it are lifted off the sector map
    // while it moves and put back on free cells if they cannot stay where they are.
    void SetState(int state, float duration);

    // 0052BBE0 (RET 8; the lever's virtual slot 44, vtable 0073E740). The spots from which a unit
    // of radius `userRadius` can use the lever, into `out` (emptied first): for each offset
    // (along, across) of the model's record in the lever table (0079C1A0, by +0xA4; none for an
    // unknown model), the position plus the facing's X axis times `along` and its Y axis times
    // `across`, the offset normalised and set at distance
    // ((1 - +0xCC) + +0xCC * record scale) * |offset| + userRadius.
    void UseSpots(LeverSpots* out, float userRadius);
};
