// The world's water level changes. See water_tweens.h.
#include "game/terrain/water_tweens.h"

#include "engine/core/core_streams.h"
#include "game/terrain/terrain.h"

void WaterTweens::SetTarget(int key, float target, int steps) {
    if (steps < 1) steps = 1;
    for (TweenEntry& e : entries) {
        if (e.key != key) continue;
        e.step = (target - e.value) / steps;
        e.steps = steps;
        return;
    }
    // A new entry, from 0.
    entries.push_back(TweenEntry{key, steps, 0.0f, target / steps});
}

bool WaterTweens::Tick() {
    bool moving = false;
    for (auto e = entries.begin(); e != entries.end();) {
        if (e->steps > 0) {
            --e->steps;
            moving = true;
            e->value = e->step + e->value;
        } else if (e->value == 0.0f) {
            // Finished and back at zero: removed.
            e = entries.erase(e);
            continue;
        }
        ++e;
    }
    return moving;
}

void WaterTweens::Save(ByteStream* stream) const {
    stream->WriteCount(entries.size());
    for (const TweenEntry& e : entries) stream->WriteValue(e);
}

void WaterTweens::Load(ByteStream* stream) {
    entries.resize(stream->ReadCount());
    for (TweenEntry& e : entries) stream->ReadValue(e);
}

void WaterTweens::ApplyTo(terrain::Terrain* terrain) const {
    for (const TweenEntry& e : entries) terrain->SetWaterLevel(e.key, e.value);
}
