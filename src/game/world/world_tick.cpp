// WorldServer: the per-tick update (timed objects, missiles, countdowns), waking units near a
// point, alarms and the game result. See world.h.
#include "game/combat/combat.h"
#include "engine/core/core_maths.h"
#include "game/objects/map_object.h"
#include "game/net/net_link.h"   // netlink::DriverSession (SetGameResult)
#include "game/script/script.h"
#include "game/ai/sector_map.h"
#include "game/spells/spell.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"
#include "game/players/player.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rounding.h"

#include <cstdlib>
#include <cstring>
#include <vector>

void WorldServer::Tick(char force) {
    ++tick;
    if ((tick & 0x3F) == 3) {
        RollWeather();
        UpdateWeather();
    }

    // Timed objects: each one's Update runs it; those that return 0 are released now and taken out of the map after
    // the walk (the walk reads the node again after the release).
    auto& timed = timedObjects.map;
    std::vector<unsigned> finished;
    for (auto& node : timed) {
        EngineObject* object = node.second;
        if (!object->RunTick()) {
            finished.push_back(static_cast<unsigned>(object->id));
            object->Release();
        }
    }
    for (unsigned objectId : finished) {
        if (auto found = timed.find(objectId); found != timed.end()) timed.erase(found);
    }
    finished.clear();

    aiMap.events.Advance(tick);
    // Every countdown counted down; true while one still runs.
    if (countdowns.Tick()) MarkPlayersDirty(lib::Bits(PlayerDirty::Countdowns));

    // Missiles: those whose Update returns 0 are unlinked and freed.
    auto& missiles = projectiles;
    for (auto next = missiles.begin(); next != missiles.end();) {
        auto current = next++;
        if ((*current)->Update() == 0) missiles.erase(current);   // the missile released
    }

    if (scriptsActive != 0 || force) MainScript()->Tick();
    SecondScript()->Tick();
    UpdateUnits();
}

void WorldServer::WakeUnitsAround(const Vector3& position, float radius) {
    static SpiralCache cache{0, -1.0f};
    int count;
    const std::int16_t* offsets = cache.Get(radius * 0.03125f, &count);   // radius in sectors
    const Point sector = WorldToCell(position) / 32;
    SectorMap* sectors = &aiMap.sectorMap;
    for (int i = 0; i < count; ++i, offsets += 2) {
        auto* units = sectors->UnitsAt(offsets[0] + sector.x, offsets[1] + sector.y);
        if (!units) continue;
        for (Unit* unit : *units) AddToUpdateMap(unit);
    }
}

void WorldServer::InvokeAlarm(int index, const float* point, char force) {
    if (index < 0 || index >= 5) return;
    WorldAlarm& alarm = alarms[index];
    if (alarm.active && !force) return;
    if (index == 0 && force) std::memset(alarms, 0, sizeof(alarms));
    alarm.active = 1;
    alarm.time = tick;
    alarm.x = point[0];
    alarm.y = point[1];

    // Every AI unit without a player hears it; the walk stops at an empty entry.
    auto& newObjects = objects.map;
    for (auto node = newObjects.begin(); node != newObjects.end() && node->second; ++node) {
        MapObject* object = node->second;
        if (Unit* unit = UnitOrNull(object)) {
            if (!unit->player) {
                AIBrain* brain = &unit->ServerController()->brain;
                brain->HearAlarm(index);
                if (brain->alwaysUpdated) AddToUpdateMap(object);   // reacting to the alarm
            }
        }
    }
}

void WorldServer::SetGameState(GameState state) {
    if (gameState != GameState::None) return;
    gameState = state;
    if (state == GameState::Network || state == GameState::Network3) {
        OpenSession();
    }
}

void WorldServer::UpdateUnits() {
    std::vector<unsigned> removedIds;    // flagged for removal
    std::vector<unsigned> sleepingIds;   // leave the update map

    // The walk stops at the end or at an entry without an object.
    if (zoneMode != ZoneMode::Battle) {
        // Every map object runs (no sleeping).
        auto& objectMap = mapObjects.map;
        for (auto node = objectMap.begin(); node != objectMap.end() && node->second; ++node) {
            EngineObject* object = node->second;
            object->RunTick();
            if (object->flags & MapObjectFlag::Removed) removedIds.push_back(static_cast<unsigned>(object->id));
        }
    } else {
        TickSleepingEffects();   // Rebirth's FixSpellEffectsOverflow
        // The units in the update map; a unit whose countdown runs out goes to sleep (leaves the map) unless its
        // controller is busy, which restarts the countdown.
        auto& awake = updateMap.map;
        for (auto node = awake.begin(); node != awake.end() && node->second; ++node) {
            EngineObject* object = node->second;
            object->RunTick();
            if (object->flags & MapObjectFlag::Removed) removedIds.push_back(static_cast<unsigned>(object->id));
            Unit* unit = UnitOrNull(object);
            if (!unit) continue;
            if (--unit->updateCountdown > 0) continue;   // updates left before it may sleep
            const UnitServer* controller = unit->ServerController();
            if (controller->moveMode2.onGuard == 0 && !controller->brain.alwaysUpdated &&
                controller->brain.memory.seesEnemy == 0)
                sleepingIds.push_back(static_cast<unsigned>(object->id));
            else
                unit->updateCountdown = UpdateCountdownFor(object);
        }
    }

    // The counted objects run too; those whose Update returns 0 leave the list (their reference released).
    std::erase_if(counted, [](EngineObject* object) {
        if (object->RunTick()) return false;
        object->Release();
        return true;
    });
    for (int i = 0; i < std::ssize(sleepingIds); ++i) {
        if (auto found = updateMap.map.find(sleepingIds[i]); found != updateMap.map.end()) updateMap.map.erase(found);
    }
    for (int i = 0; i < std::ssize(removedIds); ++i) RemoveObject(removedIds[i]);
}

void WorldServer::TickSleepingEffects() {
    // Every map object that is referenced, not flagged for removal, has effects and a companion, and is asleep gets
    // one tick taken off each effect; when one runs out, the object goes back into the update map, whose update
    // removes it (Rebirth's FixSpellEffectsOverflow: the original's sleeping objects kept their effects forever).
    for (const auto& [objectId, object] : mapObjects.map) {
        if ((object->flags & MapObjectFlag::Removed) || object->refCount == 0 || std::ssize(object->effects) == 0) continue;
        if (object->updateCountdown > 0 || !object->companion) continue;
        bool expired = false;
        for (int i = 0; i < std::ssize(object->effects); ++i) {
            if (--object->effects[i].ticksLeft <= 0) expired = true;
        }
        if (expired) AddToUpdateMap(object);
    }
}
