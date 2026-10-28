#pragma once

// The world's AI events: the viewer the brain sees through (AIViewer) and the table of events a time wheel
// and a grid hold (AIEventTable, a member of CAIMap). See docs/notes/ai.md.

#include "maths.h"
#include "unit.h"

#include <cstddef>
#include <cstdint>

#include <list>

struct AIEventPresence;
struct AIMemory;
struct FogGrid;
class CAIMap;
class CStream;

// The AI's eye: the unit being updated's position, eye and facing.
struct AIViewer {
    // The viewer moves to the owner's unit, the nearby units are listed again when due (Refresh), units in
    // the visible list it no longer sees are dropped ("lost from sight": an enemy notices 1000 / -2; a corpse
    // is listed as one first, an allied one noticing 800 / -3), then new ones are looked for (See).
    void Update(CUnitServer* owner, AIMemory* memory);
    // Every 8..11 updates (random): the units of the sector cells within the larger of the unit's two sight
    // ranges become the memory's nearby list (counted).
    void Refresh(CUnit* self, AIMemory* memory);
    // Nearby units not yet visible (corpses not yet listed) that the unit sees: within sight range, inside
    // the view cone and the line of sight; or with peripheral vision; or sensed alive. Players' units see
    // everything; others only enemies.
    void See(CUnit* self, AIMemory* memory);
    // Raises the eye by the AI map's ground level of its cell and the unit's height parts and sets the facing
    // from the unit's orientation (once per update).
    void Prepare(CUnit* self);
    // A noise of `loudness` made by `source`'s unit at `at` (null: the unit's position): every unit within 50
    // of the point (the sector map) that is hostile to the source's unit, whose noticed level is below 900
    // and whose hearing (moveFactors[1] x perception[3]) times the loudness reaches past the distance hears a
    // call of `kind` at the source unit's position (the text "Unit %i heard unit %i from %fm distance" only
    // with console filter bit 0).
    void Noise(CUnitServer* source, float loudness, int kind, const Vector3* at);
    // The noise of a moving unit: Noise of kind 0x1A at the unit, as loud as moveFactors[0] x conspicuity[3] x
    // the noise of the ground at `position` (the AI map's terrain type). Returns 0 (no presence record).
    AIEventPresence* StepNoise(const Vector3* position, CUnitServer* source);

    CAIMap* map;
    int tick;                       // the world's event table: its tick (AIEventTable::Advance)
    Vector3 eye;
    Vector3 facing;
    int cellX;                      // AI map cell of the eye
    int cellY;
    std::uint8_t ready;             // Prepare has run
};

// The cells `seer` sees as `observer` would be seen, into `grid` (cleared to one cell first): within
// sight (the two units' sight values, at least 0) inside the cone, up to the observer's mode
// height; within hearing all around; within the peripheral range in the front half.
void MarkSightCells(CUnit* seer, CUnit* observer, FogGrid* grid);

// An AI event: filed in its map cell's list for its side and in the time wheel's slot of the tick it expires.
// Records come from the pool list (AIEventPool) and go back to the free list (chained through `next`).
struct AIEvent {
    float x, y;                     // where; its cell is (x / 8, y / 8) rounded
    std::uint32_t origin[2];            // x and y again (sets them; saved)
    int expiry;                     // the tick it goes; wheel slot (expiry - 1) & 0x3FF
    std::uint32_t payload[2];           // saved and loaded; left to the creator's caller
    int sourceId;                   // the unit that made it
    int side;                       // its low 4 bits: the side list it is filed in
    AIEvent* next;                  // the cell's list for the side; the free list's link
    AIEvent* prev;                  // null for the cell's first
    AIEvent* nextInSlot;            // the wheel slot's list
};
// The pool's destructor (at exit): empties the list.

// A map cell of the event grid: the newest event of each side (lists through AIEvent::next).
struct AIEventCell {
    AIEvent* bySide[16];
};

// The free event records, chained through AIEvent::next.
extern AIEvent* g_freeEvents;

// The world's AI events (its viewer part makes the noises): a time wheel of 1024 ticks x 16 sides and a grid of 8 x 8
// cells (rows of width cells). Its tick is the viewer's `tick` (Advance sets it).
struct AIEventTable : AIViewer {
    // The wheel moves to `tick`: every event of that tick's 16 slots leaves its cell list and goes to the
    // free list; the slots empty. A first event whose cell is outside the grid faults (a null cell is read).
    void Advance(int tick);
    // Drops every event made by `controller`'s unit in the wheel column of its side (unit side & 15), as
    // Advance drops them.
    void ForgetUnit(CUnitServer* controller);
    // Load: while a byte 1 precedes, an event (its fields as Save writes them) filed in its cell and wheel
    // slot, or freed when outside the grid. Save: each cell's events by side (byte 1, x / y, origin, expiry,
    // payload, source, side), then byte 0.
    void Load(CStream* stream);
    void Save(CStream* stream);
    // A 1 x 1 grid, then Clear. Returns this.
    AIEventTable* Construct();
    // Tick -1, the wheel emptied (its events are not freed), the grid back to 1 x 1 (reallocated unless it
    // is) and its cell emptied.
    void Clear();
    // Every event of the wheel goes to the free list; the grid is freed.
    void Destroy();
    // The table for a map of sizeX x sizeY: Clear, the map kept, the grid 1 - trunc(size * -0.125) cells each
    // way (reallocated unless it has that size) and emptied. Then the 4096 bytes of "normal.dat" in the text
    // resources' "aiinfo.res" are copied. Returns 1; a missing archive or file is a fatal error, whose result
    // is returned.
    int Setup(CAIMap* aiMap, float sizeX, float sizeY);

    AIEventCell* CellOf(const AIEvent* event) const;
    void Free(AIEvent* event, int side);

    AIEvent* slots[0x400][16];      // by tick & 0x3FF, then side
    AIEventCell* cells;             // the grid's block
    AIEventCell** rows;             // the grid rows (each `width` cells)
    int width;
    int height;
};
