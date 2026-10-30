// WorldServer: the per-tick update (timed objects, missiles, countdowns), waking units near a
// point, alarms and the game result. See world.h.
#include "combat.h"
#include "core_maths.h"
#include "game_data.h"
#include "map_object.h"
#include "model.h"
#include "net_link.h"   // netlink::DriverSession (SetGameResult)
#include "script.h"
#include "sector_map.h"
#include "spell.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/crt.h"

#include "ei/rebirth.h"
#include "lib/rounding.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// SectorMap, inlined here (bounds check and address only). The unit list of sector (x, y), or null outside the
// map.
std::vector<CUnit*>* SectorUnits(SectorMap* map, int x, int y) {
    if (x < 0 || y < 0 || x >= map->width || y >= map->height) return nullptr;
    return &map->sectors[map->width * y + x].units;
}

// Sector of a world coordinate: the AI map cell (half units, rounded through a float), / 32.
int SectorOf(float world) { return lib::RoundEven(2.0f * world - 0.5f) / 32; }

} // namespace

void WorldServer::Tick(char force) {
    ++tick;
    if ((tick & 0x3F) == 3) UpdateWeather();

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

    if (scriptsActive != 0 || force) ScriptTick(MainScript());
    ScriptTick(SecondScript());
    UpdateUnits();
}

void WorldServer::WakeUnitsAround(const Vector3& position, float radius) {
    static SpiralCache cache{0, -1.0f};
    int count;
    const std::int16_t* offsets = cache.Get(radius * 0.03125f, &count);   // radius in sectors
    int sx = SectorOf(position.x);
    int sy = SectorOf(position.y);
    SectorMap* sectors = &aiMap.sectorMap;
    for (int i = 0; i < count; ++i, offsets += 2) {
        auto* units = SectorUnits(sectors, offsets[0] + sx, offsets[1] + sy);
        if (!units) continue;
        for (CUnit* unit : *units) AddToUpdateMap(unit);
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
        CMapObject* object = node->second;
        if (CUnit* unit = UnitOrNull(object)) {
            if (!unit->player) {
                AIBrain* brain = &unit->ServerController()->brain;
                brain->HearAlarm(index);
                if (brain->alwaysUpdated) AddToUpdateMap(object);   // reacting to the alarm
            }
        }
    }
}

void WorldServer::SetGameResult(int result) {
    if (gameResult != 0) return;
    gameResult = result;
    if (result == 2 || result == 3) {
        OpenSession();
        netSession.Get()->Query();    // the new session's Query (result unused)
    }
}

void WorldServer::UpdateUnits() {
    std::vector<unsigned> removedIds;    // flagged for removal
    std::vector<unsigned> sleepingIds;   // leave the update map

    // The walk stops at the end or at an entry without an object.
    if (sessionKind != 1) {
        // Every map object runs (no sleeping).
        auto& objectMap = mapObjects.map;
        for (auto node = objectMap.begin(); node != objectMap.end() && node->second; ++node) {
            EngineObject* object = node->second;
            object->RunTick();
            if (object->flags & MapObjectFlag::Removed) removedIds.push_back(static_cast<unsigned>(object->id));
        }
    } else {
        if (ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellEffectsOverflow)) TickSleepingEffects();
        // The units in the update map; a unit whose countdown runs out goes to sleep (leaves the map) unless its
        // controller is busy, which restarts the countdown.
        auto& awake = updateMap.map;
        for (auto node = awake.begin(); node != awake.end() && node->second; ++node) {
            EngineObject* object = node->second;
            object->RunTick();
            if (object->flags & MapObjectFlag::Removed) removedIds.push_back(static_cast<unsigned>(object->id));
            CUnit* unit = UnitOrNull(object);
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
    // Rebirth (a context hook): every map object that is referenced, not flagged for removal, has effects and a
    // companion, and is asleep gets one tick taken off each effect; when one runs out, the object goes back into
    // the update map, whose update removes it.
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
