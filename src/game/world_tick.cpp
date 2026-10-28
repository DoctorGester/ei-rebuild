// CWorldServer: the per-tick update (timed objects, missiles, countdowns), waking units near a
// point, alarms and the game result. See world.h.
#include "script.h"
#include "combat.h"
#include "spell.h"
#include "world.h"

#include "core_maths.h"
#include "map_object.h"
#include "model.h"
#include "net_link.h"   // netlink::DriverSession (SetGameResult)
#include "sector_map.h"
#include "unit.h"
#include "unit_ai.h"

#include "lib/containers.h"
#include "lib/crt.h"

#include "ei/rebirth.h"
#include "ei/x87.h"

#include <cstdlib>
#include <cstring>
#include <list>
#include <vector>
#include "game_data.h"

namespace {

// WakeUnitsAround's function-local static: a SpiralCache (guard bit 0 of g_wakeCacheGuard, destructor registered
// with atexit).
SpiralCache g_wakeCache = /*007AFF08*/ {};
std::uint8_t g_wakeCacheGuard = /*007AFF10*/ {};

// CSectorMap, inlined here (bounds check and address only). The unit list of sector (x, y), or null outside the
// map.
std::list<CUnit*>* SectorUnits(CSectorMap* map, int x, int y) {
    if (x < 0 || y < 0 || x >= map->width || y >= map->height) return nullptr;
    return &map->sectors[map->width * y + x].units;
}

// Sector of a world coordinate: the AI map cell (half units, rounded through a float), / 32.
int SectorOf(float world) { return x87::fistp(static_cast<float>(2.0f * world - 0.5f)) / 32; }

constinit std::vector<unsigned> g_removedIds = /*007AFEF0*/ {};    // flagged for removal
constinit std::vector<unsigned> g_sleepingIds = /*007AFEE0*/ {};   // leave the update map
// The exit functions: each frees its array's block (the fields stay).
void FreeRemovedIds() { lib::FreeStorage(g_removedIds); }
void FreeSleepingIds() { lib::FreeStorage(g_sleepingIds); }
std::uint8_t g_updateListGuards = /*007AFF00*/ {};

} // namespace

void CWorldServer::Tick(char force) {
    ++tick_;
    if ((tick_ & 0x3F) == 3) UpdateWeather();

    // Timed objects: each one's Update runs it; those that return 0 are released now and taken out of the map after
    // the walk (the walk reads the node again after the release).
    auto& timed = timedObjects_.map;
    std::list<unsigned> finished;
    for (auto node = timed.begin(); node != timed.end(); ++node) {
        CEngineObject* object = node->second;
        if (object->RunTick() == 0) {
            finished.push_back(static_cast<unsigned>(object->id_));
            object->Release();
        }
    }
    for (unsigned id : finished) {
        if (auto found = timed.find(id); found != timed.end()) timed.erase(found);
    }
    finished.clear();

    AIEvents()->Advance(tick_);
    // Every countdown counted down; true while one still runs.
    if (countdowns_.Tick()) MarkPlayersDirty(0x20);

    // Missiles: those whose Update returns 0 are unlinked and freed.
    auto& missiles = projectiles_;
    for (auto next = missiles.begin(); next != missiles.end();) {
        auto current = next++;
        if ((*current)->Update() != 0) continue;
        ReleaseCounted(*current);   // the missile released
        missiles.erase(current);
    }

    if (scriptsActive_ != 0 || force) ScriptTick(MainScript());
    ScriptTick(SecondScript());
    UpdateUnits();
}

void CWorldServer::WakeUnitsAround(const Vector3* position, float radius) {
    if (!(g_wakeCacheGuard & 1)) {
        g_wakeCache.radius = -1.0f;
        g_wakeCacheGuard |= 1;
        g_wakeCache.length = 0;
    }
    int count;
    const std::int16_t* offsets = g_wakeCache.Get(radius * 0.03125f, &count);   // radius in sectors
    int sx = SectorOf(position->x);
    int sy = SectorOf(position->y);
    CSectorMap* sectors = &aiMap_.sectorMap;
    for (int i = 0; i < count; ++i, offsets += 2) {
        auto* units = SectorUnits(sectors, offsets[0] + sx, offsets[1] + sy);
        if (!units) continue;
        for (auto unitNode = units->begin(); unitNode != units->end();) {
            CUnit* unit = *unitNode;
            ++unitNode;
            AddToUpdateMap(unit);
        }
    }
}

void CWorldServer::InvokeAlarm(int index, const float* point, char force) {
    if (index < 0 || index >= 5) return;
    WorldAlarm& alarm = alarms_[index];
    if (alarm.active && !force) return;
    if (index == 0 && force) std::memset(alarms_, 0, sizeof(alarms_));
    alarm.active = 1;
    alarm.time = tick_;
    std::memcpy(&alarm.x, &point[0], sizeof(float));
    std::memcpy(&alarm.y, &point[1], sizeof(float));

    // Every AI unit without a player hears it; the walk stops at an empty entry.
    auto& objects = objects_.map;
    for (auto node = objects.begin(); node != objects.end() && node->second; ++node) {
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

void CWorldServer::SetGameResult(int result) {
    if (gameResult_ != 0) return;
    gameResult_ = result;
    if (result == 2 || result == 3) {
        OpenSession();
        netSession_.Get()->Query();   // the new session's Query (result unused)
    }
}

void CWorldServer::UpdateUnits() {
    if (!(g_updateListGuards & 1)) {
        g_updateListGuards |= 1;
        g_removedIds = std::vector<unsigned>{};
        std::atexit(FreeRemovedIds);
    }
    if (!(g_updateListGuards & 2)) {
        g_updateListGuards |= 2;
        g_sleepingIds = std::vector<unsigned>{};
        std::atexit(FreeSleepingIds);
    }
    g_removedIds.clear();
    g_sleepingIds.clear();

    // The walk stops at the end or at an entry without an object.
    if (sessionKind_ != 1) {
        // Every map object runs (no sleeping).
        auto& objects = mapObjects_.map;
        for (auto node = objects.begin(); node != objects.end() && node->second; ++node) {
            CEngineObject* object = node->second;
            object->RunTick();
            if (object->flags_ & 1) g_removedIds.push_back(static_cast<unsigned>(object->id_));
        }
    } else {
        if (ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellEffectsOverflow)) TickSleepingEffects();
        // The units in the update map; a unit whose countdown runs out goes to sleep (leaves the map) unless its
        // controller is busy, which restarts the countdown.
        auto& awake = updateMap_.map;
        for (auto node = awake.begin(); node != awake.end() && node->second; ++node) {
            CEngineObject* object = node->second;
            object->RunTick();
            if (object->flags_ & 1) g_removedIds.push_back(static_cast<unsigned>(object->id_));
            CUnit* unit = UnitOrNull(object);
            if (!unit) continue;
            if (--unit->updateCountdown > 0) continue;   // updates left before it may sleep
            const CUnitServer* controller = unit->ServerController();
            if (controller->moveMode2.flag4 == 0 && controller->brain.alwaysUpdated == 0 &&
                controller->brain.memory.seesEnemy == 0)
                g_sleepingIds.push_back(static_cast<unsigned>(object->id_));
            else
                unit->updateCountdown = UpdateCountdownFor(object);
        }
    }

    // The counted objects run too; those whose Update returns 0 leave the list (their reference released).
    for (auto position = counted_.begin(); position != counted_.end();) {
        auto current = position++;
        if ((*current)->RunTick() == 0) {
            (*current)->Release();
            counted_.erase(current);
        }
    }
    for (int i = 0; i < std::ssize(g_sleepingIds); ++i) {
        if (auto found = updateMap_.map.find(g_sleepingIds[i]); found != updateMap_.map.end()) updateMap_.map.erase(found);
    }
    for (int i = 0; i < std::ssize(g_removedIds); ++i) RemoveObject(g_removedIds[i]);
}

void CWorldServer::TickSleepingEffects() {
    // Rebirth (a context hook): every map object that is referenced, not flagged for removal, has effects and a
    // companion, and is asleep gets one tick taken off each effect; when one runs out, the object goes back into
    // the update map, whose update removes it.
    for (const auto& [id, object] : mapObjects_.map) {
        if ((object->flags_ & 1) || object->refCount_ == 0 || std::ssize(object->effects) == 0) continue;
        if (object->updateCountdown > 0 || !object->companion) continue;
        bool expired = false;
        for (int i = 0; i < std::ssize(object->effects); ++i) {
            if (--object->effects[i].ticksLeft <= 0) expired = true;
        }
        if (expired) AddToUpdateMap(object);
    }
}
