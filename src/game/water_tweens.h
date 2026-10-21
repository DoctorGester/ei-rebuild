#pragma once

// The world's water level changes (CWorld +0x70; model_db.cpp, 006D4400).

#include "lib/mfc.h"

class CStream;   // core_streams.h

// A gradual change of one water level (script SetWaterLevel(index, level, time)): `value` moves
// by `step` per world update for `steps` updates.
struct TweenEntry {
    int key;        // water index
    int steps;      // updates left
    float value;
    float step;
};
static_assert(sizeof(TweenEntry) == 16);

// CArray<TweenEntry> of the world: new entries zero-filled, never constructed. RemoveAll 006D43E0
// and SetSize 006D4840 are registered on the template (model_db.cpp); the rest are members.
struct WaterTweens : mfc::CArray<TweenEntry> {
    // 006D4410 (via 00593FA0). The entry for `key` (made when there is none) moves to `target` over
    // `steps` updates (at least one).
    void SetTarget(int key, float target, int steps);
    // 006D44E0. One update of every entry; the ones finished and back at zero are removed. True
    // while any entry moves.
    bool Tick();
    // 006D4570 (006D47E0 identical): the count, then the entries.
    void Save(CStream* stream) const;
    // 006D45D0. Reads the array and applies each entry's value to `terrain` (006CC7F0).
    void Load(CStream* stream, void* terrain);
    // 006D4660. Reads the array only (the inlined SetSize of another CArray instantiation).
    void LoadEntries(CStream* stream);
    // The read part of both loads: the count (the original reads it into a slot that still holds
    // `unread` if the stream writes nothing), SetSize, then the entries.
    void ReadEntries(CStream* stream, int unread);
};
static_assert(sizeof(WaterTweens) == sizeof(mfc::CArray<TweenEntry>));
