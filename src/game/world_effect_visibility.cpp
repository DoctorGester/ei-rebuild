// CEffectShowVisibility: a unit's sight shown in the fog of war (clairvoyance). See world_effect.h
// and docs/notes/effects.md ("Missiles and visibility").
#include "world_effect.h"

#include "ai_grids.h"
#include "map_object.h"
#include "world.h"
#include "unit.h"
#include "unit_ai.h"

#include "../lib/crt.h"

#include <cstring>


namespace {

// The fog manager; VisFogManagerAdd makes a fog object, already counted once by the manager's list.
ClientArrayManager* FogManager() { return g_clientWorld->arrayManager_; }

void ReleaseFog(CVisibilityFog*& fog) {
    CVisibilityFog* old = fog;
    fog = nullptr;
    if (old != nullptr) old->Release();
}

} // namespace


CEffectShowVisibility::CEffectShowVisibility() {
    fog_ = nullptr;
    SearchGrid<std::uint8_t>& cells = grid_.cells;
    cells.width = 1;
    cells.height = 1;
    cells.data = new std::uint8_t[1];
    cells.rows = new std::uint8_t*[cells.height];
    for (int row = 0; row < cells.height; ++row) cells.rows[row] = cells.data + row * cells.width;
    observerId_ = -1;
    seerId_ = -1;
}

// The destructor body resets twice (inline, then the virtual), frees the grid and destroys the base.
CEffectShowVisibility::~CEffectShowVisibility() {
    Reset();
    Reset();
    FreeGrid(grid_.cells);
}

// The base is not reset: only the grid (taken out of the fog first), the units and the fog.
void CEffectShowVisibility::Reset() {
    grid_.Clear();
    if (fog_ != nullptr) fog_->Reveal(&grid_);
    observerId_ = -1;
    seerId_ = -1;
    ReleaseFog(fog_);
}

void CEffectShowVisibility::SetUnits(int observerId, int seerId) {
    observerId_ = observerId;
    seerId_ = seerId;
}

void CEffectShowVisibility::Start(int update) {
    if (update == 0) {
        auto* created = static_cast<CVisibilityFog*>(VisFogManagerAdd(FogManager()));
        CVisibilityFog* old = fog_;
        fog_ = created;
        if (created != nullptr) ++created->refCount_;
        if (old != nullptr) old->Release();
    }
    if (fog_ != nullptr) fog_->Reveal(&grid_);
}

int CEffectShowVisibility::Unpack(int update, DeltaField* field) {
    CMemoryStream stream;
    if (field->ReadStream(&stream) == 0) return 0;
    grid_.Read(&stream);
    Start(update);
    return 1;
}

void CEffectShowVisibility::Pack(DeltaField* field) {
    CMemoryStream stream;
    grid_.Write(&stream);
    field->WriteStreamBoth(&stream);
}

int CEffectShowVisibility::RunTick() {
    if (!WorldContext()->IsServer()) return 1;
    CUnit* seer = ObjectWorld(this)->FindUnit(seerId_);
    CUnit* observer = ObjectWorld(this)->FindUnit(observerId_);
    if (seer == nullptr || observer == nullptr) return 0;
    std::memcpy(&localPosition_, &seer->localPosition_.x, sizeof(Vector3));
    // The squares `seer` sees, with `observer`'s skills.
    MarkSightCells(seer, observer, &grid_);
    MarkChanged();
    return 1;
}

void CEffectShowVisibility::Load(CStream* s) {
    s->Read(&observerId_, 4);
    s->Read(&seerId_, 4);
}

void CEffectShowVisibility::Save(CStream* s) {
    int copy = observerId_;
    s->Write(&copy, 4);
    copy = seerId_;
    s->Write(&copy, 4);
}
