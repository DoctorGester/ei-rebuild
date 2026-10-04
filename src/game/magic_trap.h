#pragma once

// Magic traps (map objects of class id 0x3B, 0x290 bytes, vtable 0073E63C, constructed by
// 00529DD0; the zone file's "MagicTrap" records). Names provisional; only the members the rewrite
// uses are known (magic_trap.cpp): +0x08 flags_ (0x80000: armed, the script's ActivateTrap), +0x1C
// the position, +0x238 the sides that set it off (bit per unit side +0x250), +0x23C its spell
// (null: none), +0x240 an MFC array of circles {x, y, radius} (data +0x244, count +0x248), +0x258
// / +0x25C the points it casts at, +0x268 the units inside (an MFC CList of counted references:
// head +0x26C, tail +0x270, count +0x274), +0x284 the period and +0x288 the countdown between
// casts, +0x28C cast at one unit only. See docs/notes/units.md ("Magic traps").
struct MagicTrap {
    // 0052A680. Called from the trap's update (slot 6, 0052AC20) while the world is live, it has
    // a spell and its period is not positive. The units inside that left every circle
    // (or a unit that is dead, in the first circle that holds it) are dropped. An armed trap then
    // takes in the living units of its sides found in its circles (0052A920) and casts its spell
    // (0052A4B0) when any of them is new.
    void UpdateVictims();
};

// A circle of a trap's area (0xC bytes).
struct TrapCircle {
    float x;
    float y;
    float radius;
};
static_assert(sizeof(TrapCircle) == 0xC);
