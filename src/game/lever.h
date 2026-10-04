#pragma once

// Levers (units with positions; script.md "levers"): +0xA4 the model name, +0xC8 the shown
// position (0..1), +0x134 the radius, +0x238 the state, +0x23C the number of states, +0x240
// cycling (wraps instead of stopping at the last state), +0x250 / +0x254 the animation's start
// and end position, +0x258 the end tick, +0x25C 1 / duration, +0x261 the AI map flag kept while
// the lever moves. Only the members used by the rewrite are known (unit_lever.cpp).
struct Lever {
    // 0052B7A0 (RET 8). Sets state `state` (negative: the next one, wrapping when cycling) and
    // moves the lever there over `duration` ticks (negative: the model's default from 0079C1AC,
    // else 10; zero or less: at once). The units standing around it are lifted off the sector map
    // while it moves and put back on free cells if they cannot stay where they are.
    void SetState(int state, float duration);
};
