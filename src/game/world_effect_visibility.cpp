// EffectShowVisibility: a unit's sight shown in the fog of war (clairvoyance). See world_effect.h
// and docs/notes/effects.md ("Missiles and visibility").
#include "ai_map.h"
#include "map_object.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"
#include "world_effect.h"

#include "lib/crt.h"

#include <cstring>

EffectShowVisibility::EffectShowVisibility() {
    grid.cells.Resize(1, 1);
    observerId = -1;
    seerId = -1;
}

// The destructor body resets twice (inline, then the virtual); the grid frees itself.
EffectShowVisibility::~EffectShowVisibility() {
    Reset();
    Reset();
}

// The base is not reset: only the grid (taken out of the fog first), the units and the fog.
void EffectShowVisibility::Reset() {
    grid.Clear();
    if (fog != nullptr) fog->Reveal(&grid);
    observerId = -1;
    seerId = -1;
    fog.Reset();
}

void EffectShowVisibility::SetUnits(int newObserverId, int newSeerId) {
    observerId = newObserverId;
    seerId = newSeerId;
}

void EffectShowVisibility::Start(int update) {
    if (update == 0) {
        auto* created = static_cast<VisibilityFog*>(VisFogManagerAdd(g_clientWorld.arrayManager));
        fog.Assign(created);
    }
    if (fog != nullptr) fog->Reveal(&grid);
}

int EffectShowVisibility::Unpack(int update, DeltaField* field) {
    MemoryStream stream;
    if (field->ReadStream(&stream) == 0) return 0;
    grid.Read(&stream);
    Start(update);
    return 1;
}

void EffectShowVisibility::Pack(DeltaField* field) {
    MemoryStream stream;
    grid.Write(&stream);
    field->WriteStreamBoth(&stream);
}

bool EffectShowVisibility::RunTick() {
    if (!WorldContext()->IsServer()) return true;
    Unit* seer = OwningWorld()->FindUnit(seerId);
    Unit* observer = OwningWorld()->FindUnit(observerId);
    if (seer == nullptr || observer == nullptr) return false;
    std::memcpy(&localPosition, &seer->localPosition.x, sizeof(Vector3));
    // The squares `seer` sees, with `observer`'s skills.
    MarkSightCells(seer, observer, &grid);
    MarkChanged();
    return true;
}

void EffectShowVisibility::Load(ByteStream* s) {
    s->ReadValue(observerId);
    s->ReadValue(seerId);
}

void EffectShowVisibility::Save(ByteStream* s) {
    int copy = observerId;
    s->WriteValue(copy);
    copy = seerId;
    s->WriteValue(copy);
}
