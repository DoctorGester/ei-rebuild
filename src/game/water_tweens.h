#pragma once

// The world's water level changes (CWorld::countdowns_; model_db.cpp).

#include "lib/mfc.h"

#include <vector>

class CStream;
class Terrain;

// A gradual change of one water level (script SetWaterLevel(index, level, time)): `value` moves
// by `step` per world update for `steps` updates.
struct TweenEntry {
    int key;        // water index
    int steps;      // updates left
    float value;
    float step;
};
static_assert(sizeof(TweenEntry) == 16);

// CArray<TweenEntry> of the world: new entries zero-filled, never constructed. RemoveAll
// and SetSize are the template's; the rest are members.
struct WaterTweens : std::vector<TweenEntry> {
    // The entry for `key` (made when there is none) moves to `target` over
    // `steps` updates (at least one).
    void SetTarget(int key, float target, int steps);
    // One update of every entry; the ones finished and back at zero are removed. True
    // while any entry moves.
    bool Tick();
    // The count, then the entries (identical copies).
    void Save(CStream* stream) const;
    // Reads the array and applies each entry's value to `terrain`.
    void Load(CStream* stream, Terrain* terrain);
    // Reads the array only (the inlined SetSize of another CArray instantiation).
    void LoadEntries(CStream* stream);
    // The read part of both loads: the count (the game reads it into a slot that still holds
    // `unread` if the stream writes nothing), SetSize, then the entries.
    void ReadEntries(CStream* stream, int unread);
};
