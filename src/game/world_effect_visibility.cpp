// CEffectShowVisibility: a unit's sight shown in the fog of war (clairvoyance). See world_effect.h
// and docs/notes/effects.md ("Missiles and visibility").
#include "ai_map.h"
#include "map_object.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"
#include "world_effect.h"

#include "lib/crt.h"

#include <cstring>

CEffectShowVisibility::CEffectShowVisibility() {
    grid.cells.Resize(1, 1);
    observerId = -1;
    seerId = -1;
}

// The destructor body resets twice (inline, then the virtual); the grid frees itself.
CEffectShowVisibility::~CEffectShowVisibility() {
    Reset();
    Reset();
}

// The base is not reset: only the grid (taken out of the fog first), the units and the fog.
void CEffectShowVisibility::Reset() {
    grid.Clear();
    if (fog != nullptr) fog->Reveal(&grid);
    observerId = -1;
    seerId = -1;
    fog.Reset();
}

void CEffectShowVisibility::SetUnits(int newObserverId, int newSeerId) {
    observerId = newObserverId;
    seerId = newSeerId;
}

void CEffectShowVisibility::Start(int update) {
    if (update == 0) {
        auto* created = static_cast<CVisibilityFog*>(VisFogManagerAdd(g_clientWorld.arrayManager));
        fog.Assign(created);
    }
    if (fog != nullptr) fog->Reveal(&grid);
}

int CEffectShowVisibility::Unpack(int update, DeltaField* field) {
    CMemoryStream stream;
    if (field->ReadStream(&stream) == 0) return 0;
    grid.Read(&stream);
    Start(update);
    return 1;
}

void CEffectShowVisibility::Pack(DeltaField* field) {
    CMemoryStream stream;
    grid.Write(&stream);
    field->WriteStreamBoth(&stream);
}

bool CEffectShowVisibility::RunTick() {
    if (!WorldContext()->IsServer()) return true;
    CUnit* seer = World()->FindUnit(seerId);
    CUnit* observer = World()->FindUnit(observerId);
    if (seer == nullptr || observer == nullptr) return false;
    std::memcpy(&localPosition, &seer->localPosition.x, sizeof(Vector3));
    // The squares `seer` sees, with `observer`'s skills.
    MarkSightCells(seer, observer, &grid);
    MarkChanged();
    return true;
}

void CEffectShowVisibility::Load(CStream* s) {
    s->ReadValue(observerId);
    s->ReadValue(seerId);
}

void CEffectShowVisibility::Save(CStream* s) {
    int copy = observerId;
    s->WriteValue(copy);
    copy = seerId;
    s->WriteValue(copy);
}
