// A lever's state change (0052B7A0) and the spots it is used from (0052BBE0). See
// docs/notes/units.md ("Levers").
#include "lever.h"
#include "combat.h"

#include "ai_map.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp_hash.h"
#include "lib/mfc_slots.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include "originals_ai.h"
#include "originals_engine.h"
#include "originals_units.h"
#include "originals_library.h"
#include "game_data.h"

namespace {

// The object's world [00455950]; the sector map's units in a radius [005C0BD0: position, list,
// radius], Remove [005C1010], Add [005C0DF0]; the AI map's refresh [005B8B30 / 005B8B80], cell
// tests [005BA830 CanLeaveCell, 005BA9D0 FindFreeCellNear (unit, from, out)]; the figure's
// animation [005179E0 (model name)]; MarkChanged [0050E9F0 (part)]; the unit's controller resync
// [00554450, on +0x240].

} // namespace

void Lever::SetState(int wanted, float duration) {
    // FCOMP + TEST AH,1: a negative duration takes the model's default.
    EI_NAN_AUDIT(duration);
    if (duration < 0.0f) {
        // The model's record by name: the table's operator[] (00464750; an unknown name gets a null entry).
        const LeverRecord* record = static_cast<const LeverRecord*>(stlp::RecordNamed(reinterpret_cast<stlp::RecordsByName*>(&TheLeverTable()), nullptr, name));
        duration = record ? static_cast<float>(static_cast<std::int64_t>(record->duration)) : 10.0f;
    }
    int& current = state;
    int states = stateCount;
    if (wanted < 0) {
        if (cycling != 0) {
            current = (current + 1) % states;
        } else {
            int next = current + 1;
            current = next < states - 1 ? next : states - 1;
        }
    } else {
        int chosen = wanted < states - 1 ? wanted : states - 1;
        current = chosen <= 0 ? 0 : chosen;
    }

    // Only on the server (the world's slot 26).
    auto* world = static_cast<CWorldServer*>(ObjectWorld(this));
    if (!world->IsServer()) return;
    mfc::CTypedPtrList<CUnit> around{mfc::impl::PtrListClass::Vtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};   // CPtrList on the stack
    CSectorMap* sectors = world->Sectors();
    float reach = radius + 0.5f;
    sectors->FindUnitsInRadius(&position, &around, reach);
    for (auto* node = around.head; node;) {
        auto* next = node->next;
        SectorMapRemove(sectors, nullptr, AsEngineObject(node->data));
        node = next;
    }
    CAIMap* map = world->AIMap();
    std::memcpy(&map->refreshUnits, &keepsMapFlag, 1);   // the byte as it is
    map->BeginRefresh();
    SectorMapRemove(sectors, nullptr, AsEngineObject(this));

    states = stateCount;
    float travel = states > 1 ? static_cast<float>(static_cast<double>(state) / (states - 1)) : 0.0f;
    // FCOMP + TEST AH,0x41: timed only for a duration above zero.
    if (duration > 0.0f) {
        to = travel;
        std::memcpy(&from, &scale.y, sizeof(from));   // copied as a dword
        endTick = (static_cast<float>(world->tick_) + duration) + 1.0f;
        rate = 1.0f / duration;
    } else {
        to = travel;
        from = travel;
        endTick = 0.0f;
        rate = 0.0f;
    }
    scale.y = travel;   // the shown position
    SetUpAnimatedFigure(name);
    static_cast<CMapObject*>(this)->MarkChanged(5);
    SectorMapAdd(sectors, nullptr, AsEngineObject(this));
    map->EndRefresh();
    map->refreshUnits = true;

    for (auto* node = around.head; node;) {
        auto* next = node->next;
        CUnit* unit = node->data;
        if (!map->CanLeaveCell(unit)) {
            Vector3* at = &unit->position;
            Vector3 free = {at->x, at->y, at->z};
            map->FindFreeCellNear(unit, at, &free);
            std::memcpy(at, &free, sizeof(free));
            unit->ServerController()->ResyncWithUnit();
        }
        SectorMapAdd(sectors, nullptr, unit);
        node = next;
    }
    around.RemoveAll();
}

EI_REWRITTEN(0x0052B7A0, Lever::SetState);

namespace {

// The lever table's record by model name [0052CB40, thiscall on the table: operator[] of its map
// at +0xC, so an unknown name gets a null record]; the spots' SetAtGrow [0052CC50 (index, value)].

stlp::RecordsByName* LeverTableByName() { return reinterpret_cast<stlp::RecordsByName*>(&TheLeverTable()); }

} // namespace

void Lever::CollectUseSpots(LeverSpots* out, float userRadius) {
    // CArray::RemoveAll, inlined.
    if (out->data) {
        gamecrt::operator_delete(out->data);
        out->data = nullptr;
    }
    out->maxSize = 0;
    out->size = 0;
    const LeverRecord* record = static_cast<const LeverRecord*>(stlp::RecordNamed(LeverTableByName(), nullptr, name));
    if (!record || record->spotCount == 0) return;
    const Quaternion& facing = orientation;
    // The count is read again each time; a count above 2 reads on into the record (sic).
    for (std::uint32_t i = 0; i < record->spotCount; ++i) {
        const float* spot = &record->spots[0][0] + 2 * i;
        Vector3 at{position.x, position.y, position.z};
        float along = spot[0];
        float across = spot[1];
        float length = static_cast<float>(std::sqrt(across * across + along * along));
        // FCOM 0.0; TEST AH,0x41: normalised only for a length above zero.
        if (length > 0.0f) {
            along = along / length;
            across = across / length;
        }
        float phase = scale.z;
        float distance = ((1.0f - phase) + phase * record->farScale) * length + userRadius;
        Vector3 axis;
        facing.AxisX(&axis);
        at.x = axis.x * along * distance + at.x;
        at.y = axis.y * along * distance + at.y;
        at.z = axis.z * along * distance + at.z;
        facing.AxisY(&axis);
        at.x = axis.x * across * distance + at.x;
        at.y = axis.y * across * distance + at.y;
        at.z = axis.z * across * distance + at.z;
        out->SetAtGrow(out->size, at);
    }
}
EI_REWRITTEN(0x0052BBE0, Lever::CollectUseSpots);
