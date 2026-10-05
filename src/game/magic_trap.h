#pragma once

// Magic traps (map objects of class id 0x3B, 0x290 bytes, vtable 0073E63C, constructed by
// 00529DD0; the zone file's "MagicTrap" records). Names provisional; only the members the rewrite
// uses are known (magic_trap.cpp): +0x08 flags_ (0x80000: armed, the script's ActivateTrap), +0x1C
// the position, +0x238 the sides that set it off (bit per unit side +0x250), +0x23C its spell
// (null: none), +0x240 an MFC array of circles {x, y, radius} (data +0x244, count +0x248), +0x258
// / +0x25C the points it casts at ({x, y} pairs), +0x268 the units inside (an MFC CList of
// counted references: head +0x26C, tail +0x270, count +0x274), +0x284 the period and +0x288 the
// countdown between casts, +0x28C cast at one unit only. See docs/notes/units.md ("Magic traps").

#include "lib/mfc.h"

#include <cstdint>

class CEngineObject;
struct MagicTrapRecord;
struct UnitStream;

struct MagicTrap {
    // 00529DD0. The constructor (the factory 00512730, class id 0x3B): the map object (0050E2D0),
    // no spell, the two arrays (circles vtable 0073C68C, points 0073C680) and the list of units
    // inside (vtable 0073E730, blocks of 10) empty, the vtable 0073E63C; then the map object's
    // Create (0050E770, called directly), sides -1, flags_ |= 0x80040, the spell released, both
    // arrays SetSize(0, -1), the list emptied, countdown 0, +0x28C 0, period 10. Returns this.
    MagicTrap* Construct();
    // 00529F90. The destructor body (the deleting destructor 00529F70, slot 0): the map object's
    // Destroy (0050E840, called directly), the spell released, both arrays SetSize(0, -1), the
    // list emptied twice (RemoveAll, then CList's destructor), the arrays' storage freed, then
    // the map object's destructor body (0050E660).
    void DestructBody();
    // 0052B070 (virtual slot 10, RET 4). The map object part (0050F900), the spell's id (-1:
    // none), the circles (count, then 12 bytes each), the units inside (count, then each id
    // through 0050E290), the points (count, then 8 bytes each), the period, countdown, +0x28C and
    // the sides.
    void SaveTrap(UnitStream* stream);
    // 0052ADC0 (virtual slot 9, RET 4). Reads what SaveTrap writes: the spell found by id in the
    // spell manager (the context's slot 22, 00403F40), the circles and points through SetSize(n,
    // -1), the units inside appended by 0052AF20 (resolved in the trap's context).
    void LoadTrap(UnitStream* stream);

    // 0052A2C0 (RET 4). Sets the trap up from its zone record (the zone loader 005927C0): the
    // sides, the spell made from the record's text by the spell manager (the context's slot 22,
    // 00689030; the old one released), the position, the circles and points copied, the units
    // inside forgotten (each reference released, the list's blocks freed), the period with the
    // countdown at 0, and +0x28C from the record's byte.
    void SetUp(const MagicTrapRecord* record);

    // 0052A680. Called from the trap's update (slot 6, 0052AC20) while the world is live, it has
    // a spell and its period is not positive. The units inside that left every circle
    // (or a unit that is dead, in the first circle that holds it) are dropped. An armed trap then
    // takes in the living units of its sides found in its circles (0052A920) and casts its spell
    // (0052A4B0) when any of them is new.
    void UpdateVictims();

    // 0052A4B0 (RET 4). Casts the trap's spell (the world server's 0058E850, no caster) from the
    // trap's position raised by the ground height under it: at each of its points {x, y, 0} unless
    // the spell has flag 0x10000000 or there are none; otherwise at each unit of `units` (target
    // and point the unit), with +0x28C first narrowing `units` to the last one closer than 1e30
    // (squared; NaN counts). Then the countdown starts again (+0x288 = +0x284). Nothing without
    // units or a spell; nothing either (the countdown kept) when +0x28C finds none.
    void CastAtUnits(mfc::CTypedPtrList<CEngineObject>* units);
};

// A circle of a trap's area (0xC bytes).
struct TrapCircle {
    float x;
    float y;
    float radius;
};
static_assert(sizeof(TrapCircle) == 0xC);

// A trap's area (+0x240): an MFC CArray of circles.
struct TrapArea {
    void* vtable;
    TrapCircle* circles;    // +0x04
    int count;              // +0x08
    int maxCount;
    int growBy;
};
static_assert(sizeof(TrapArea) == 0x14);

// A trap's points (+0x254): an MFC CArray of {x, y} pairs.
struct TrapPoints {
    void* vtable;
    float* points;          // +0x04, two floats each
    int count;              // +0x08
    int maxCount;
    int growBy;
};
static_assert(sizeof(TrapPoints) == 0x14);

// The MagicTrap part of a zone object record (at record + 0x8C, 005927C0).
struct MagicTrapRecord {
    std::uint32_t sides;        // +0x00
    const char* spell;          // +0x04 the spell's text (a CString)
    float position[3];          // +0x08
    TrapArea area;              // +0x14
    TrapPoints points;          // +0x28
    std::uint32_t period;       // +0x3C
    std::uint8_t singleTarget;  // +0x40
};
static_assert(sizeof(MagicTrapRecord) == 0x44);

// 0052A920 (__fastcall, RET 8). Empties `out` (an MFC CPtrList, RemoveAll inlined), then for each
// circle of `area` asks the sector map (`context` + 0x294, 005C0BD0) for the units within its
// radius of {x, y, 0} into a CPtrList of its own (vtable 0073D454, blocks of 10) and appends to
// `out` (AddTail inlined) each one alive (slot 55 returns 0) whose side (+0x250) has its bit in
// `sides`; a unit in two circles is appended twice. `context` is the trap's map object context
// (the world). See docs/notes/units.md ("Sixth round").
void __fastcall FindTrapVictims(void* context, const TrapArea* area, std::uint32_t sides,
                                mfc::CTypedPtrList<CEngineObject>* out);
