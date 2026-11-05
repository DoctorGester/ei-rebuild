#pragma once

// The world's water level changes (World::countdowns; model_db.cpp).

#include "lib/bytes.h"

#include <vector>

class ByteStream;
namespace terrain { class Terrain; }

// A gradual change of one water level (script SetWaterLevel(index, level, time)): `value` moves
// by `step` per world update for `steps` updates.
struct TweenEntry {
    int key = 0;    // water index
    int steps = 0;  // updates left
    float value = 0.0f;
    float step = 0.0f;
};
static_assert(sizeof(TweenEntry) == 16);   // saved as its 16 bytes
constexpr bool NoPadding(const TweenEntry*) {
    return lib::FieldsFill<&TweenEntry::key, &TweenEntry::steps, &TweenEntry::value, &TweenEntry::step>();
}

// The world's water level changes, in the order they were first set.
struct WaterTweens {
    // The entry for `key` (made when there is none) moves to `target` over
    // `steps` updates (at least one).
    void SetTarget(int key, float target, int steps);
    // One update of every entry; the ones finished and back at zero are removed. True
    // while any entry moves.
    bool Tick();
    // The count, then the entries (identical copies).
    void Save(ByteStream* stream) const;
    // The count, the array resized to it, then the entries.
    void Load(ByteStream* stream);
    // Sets each entry's value as `terrain`'s water level.
    void ApplyTo(terrain::Terrain* terrain) const;

    std::vector<TweenEntry> entries;
};
