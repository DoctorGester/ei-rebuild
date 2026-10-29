// A lever's state change and the spots it is used from. See
// docs/notes/units.md ("Levers").
#include "lever.h"
#include "combat.h"

#include "ai_map.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "sector_map.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include <cstdint>
#include <cstring>

#include <cmath>
#include <list>

void Lever::SetState(int wanted, float duration) {
    // A negative duration takes the model's default.
    EI_NAN_AUDIT(duration);
    if (duration < 0.0f) {
        const LeverRecord* record = g_leverTable.Named(name);
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

    auto* world = ServerWorld();
    if (!world->IsServer()) return;
    CSectorMap* sectors = &world->aiMap.sectorMap;
    float reach = bounds.radius + 0.5f;
    std::list<CUnit*> around = sectors->FindUnitsInRadius(localPosition, reach);
    for (auto node = around.begin(); node != around.end();) {
        auto next = std::next(node);
        sectors->Remove(*node);
        node = next;
    }
    CAIMap* map = &world->aiMap;
    map->refreshUnits = keepsMapFlag;   // the byte as it is
    map->BeginRefresh();
    sectors->Remove(this);

    states = stateCount;
    float travel = states > 1 ? static_cast<float>(static_cast<double>(state) / (states - 1)) : 0.0f;
    if (duration > 0.0f) {
        to = travel;
        from = scale.y;   // bit for bit
        endTick = (static_cast<float>(world->tick) + duration) + 1.0f;
        rate = 1.0f / duration;
    } else {
        to = travel;
        from = travel;
        endTick = 0.0f;
        rate = 0.0f;
    }
    scale.y = travel;   // the shown position
    SetUpAnimatedFigure(name);
    MarkChanged(5);
    sectors->Add(this);
    map->EndRefresh();
    map->refreshUnits = true;

    for (auto node = around.begin(); node != around.end();) {
        auto next = std::next(node);
        CUnit* unit = *node;
        if (!map->CanLeaveCell(unit)) {
            Vector3* at = &unit->localPosition;
            Vector3 free = {at->x, at->y, at->z};
            map->FindFreeCellNear(unit, *at, &free);
            *at = free;
            unit->ServerController()->ResyncWithUnit();
        }
        sectors->Add(unit);
        node = next;
    }
    around.clear();
}

std::vector<Vector3> Lever::UseSpots(float userRadius) {
    std::vector<Vector3> spots;
    const LeverRecord* record = g_leverTable.Named(name);
    if (!record || record->spotCount == 0) return spots;
    const Quaternion& facing = localRotation;
    // The count is read again each time; a count above 2 reads on into the record (sic).
    for (std::uint32_t i = 0; i < record->spotCount; ++i) {
        const float* spot = &record->spots[0][0] + 2 * i;
        Vector3 at{localPosition.x, localPosition.y, localPosition.z};
        float along = spot[0];
        float across = spot[1];
        float length = static_cast<float>(std::sqrt(across * across + along * along));
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
        spots.push_back(at);
    }
    return spots;
}
