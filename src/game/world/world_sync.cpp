// WorldServer: keeping each player's view of the world up to date (single player too: the local
// player is a client of the server). SyncPlayer runs once per world update for every player
// SendZone sends a player the zone it has to load. See world.h and docs/notes/world.md.
#include "game/combat/combat.h"
#include "engine/core/core_maths.h"
#include "engine/core/core_streams.h"
#include "game/objects/map_object.h"
#include "game/world/hoover.h"
#include "game/app/options.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"
#include "game/map/map_database.h"

#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/float_select.h"
#include "lib/rounding.h"

#include <bit>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

constexpr float kHearingScale = 2.0f;   // the units' perception scale

// Distance limits.
constexpr float kViewRange = 100.0f;        // objects seen around the camera
constexpr float kHearingMargin = 5.0f;    // added to the units' hearing radius when a player is synced

constexpr MapObjectType kTypeNoSync = MapObjectType::MagicTrap;   // never sent


// Whether `point` lies outside the camera's view range around the player's last position, in the ground plane; NaN
// counts as inside.
bool OutOfView(const Vector3& point, const PlayerServer* state) {
    return DistanceSquaredXY(point, state->lastPosition) >= kViewRange * kViewRange;
}

// The main menu's scene is sent whole, wherever the player's last position is: the menu's camera is the client's own
// path, started by the menu screen that the server's game state message opens, so the server learns it a world
// update after that message. With the view range, a return from a game (the camera still over the game's map) sent
// the menu's objects (the signpost, the troll, the fire) an update after the menu was shown, and its first frames
// had the menu's map and lights without them (docs/notes/world.md, "The view range").
bool SeesWholeZone(const WorldServer* world) { return world->zoneMode == ZoneMode::MainMenu; }

// The map objects a player should know: around its camera (or all of them), its own units, the
// objects named in scripts, and every unit within hearing range of one of its units. `out` is the
// objects are appended to `out`.
void CollectMapObjects(WorldServer* world, Player* player, PlayerServer* state, std::vector<MapObject*>& out) {
    if (world->zoneMode == ZoneMode::Battle && options::g_options.drawVisibleOnly == 0) {
        // Every map object near the camera, and the player's units anywhere; nothing else.
        for (auto node = world->mapObjects.map.begin(); node != world->mapObjects.map.end() && node->second; ++node) {
            MapObject* object = node->second;
            if (object->MapType() == kTypeNoSync) continue;
            if (!OutOfView(object->localPosition, state)) {
                out.push_back(object);
                continue;
            }
            if (Unit* unit = UnitOrNull(object); unit && unit->player == player) out.push_back(object);
        }
        return;
    }

    MapObjectIdMap& source = world->zoneMode == ZoneMode::Battle ? world->otherMapObjects : world->mapObjects;
    if (world->zoneMode == ZoneMode::Base) {
        for (auto node = source.map.begin(); node != source.map.end() && node->second; ++node) {
            MapObject* object = node->second;
            if (object->MapType() != kTypeNoSync && !(object->flags & MapObjectFlag::Invisible)) out.push_back(object);
        }
    } else {
        for (auto node = source.map.begin(); node != source.map.end() && node->second; ++node) {
            MapObject* object = node->second;
            if (object->MapType() == kTypeNoSync) continue;
            if (SeesWholeZone(world) || !OutOfView(object->localPosition, state)) out.push_back(object);
        }
    }
    if (world->zoneMode != ZoneMode::Battle) return;

    // The units always sent.
    for (const ScriptObjectRef& unit : state->partyUnits) out.push_back(unit.object);
    for (auto node = world->namedObjects.map.begin(); node != world->namedObjects.map.end() && node->second; ++node)
        out.push_back(node->second);

    // The units each of the player's units can hear.
    auto& units = player->units;
    for (auto node = units.begin(); node != units.end() && node->second; ++node) {
        Unit* unit = node->second;
        for (Unit* heard : world->aiMap.sectorMap.FindUnitsInRadius(unit->localPosition, HearingRadius(unit, kHearingMargin)))
            out.push_back(heard);
    }
}

// The timed objects (spells, effects) a player should know: those whose reach (a virtual, squared) covers its
// camera; in the main menu every one.
void CollectTimedObjects(WorldServer* world, PlayerServer* state, std::vector<Effect*>& out) {
    for (auto node = world->timedObjects.map.begin(); node != world->timedObjects.map.end() && node->second; ++node) {
        Effect* timed = node->second;
        if (SeesWholeZone(world)) {
            out.push_back(timed);
            continue;
        }
        const Vector3* position = timed->Position(&state->partyUnits);
        float distance = DistanceSquaredXY(*position, state->lastPosition);
        float reach = timed->Radius();
        float reach2 = timed->Radius() * reach;
        if (reach2 > distance) out.push_back(timed);
    }
}

} // namespace

float HearingRadius(Unit* unit, float margin) {
    auto* controller = unit->ServerController();
    float perception = controller->moveFactors[MoveFactor::Sight] * controller->stats.perception[Sense::Sight].current;
    float minimum = controller->stats.perception[Sense::Near].current;
    float range = MaxKeepB(perception, minimum);   // branch-free select
    return range * kHearingScale + margin;
}

int WorldServer::SyncPlayer(Player* player) {
    PlayerServer* state = player->Server();
    if (state->state == PlayerServer::State::Joined && zoneLoaded != 0) {
        std::uint32_t dirty = player->dirty;
        if (dirty & PlayerDirty::Zone) {
            SendZone(player);
            state->SetDisconnect();
            state->SendItems();
            return state->Flush();
        }
        if (dirty & PlayerDirty::Weather) {
            int strength = lib::RoundEven(weatherStrength);
            state->SendWeather(static_cast<std::uint8_t>(weather), static_cast<std::uint16_t>(strength));
        }
        if (player->dirty & PlayerDirty::Time) {
            const Clock clock = GetTime();
            state->SendTime(clock.hours, clock.speed);
        }
        if (player->dirty & PlayerDirty::Wind) {
            state->SendWind(windDirection.x, windDirection.y, windDirection.z, windStrength);
        }
        if (player->dirty & PlayerDirty::Countdowns) state->SendCountdowns(&countdowns);

        // Map objects.
        std::vector<MapObject*> collected;
        std::vector<MapObject*> added;
        std::vector<MapObject*> gone;
        std::vector<MapObject*> same;
        CollectMapObjects(this, player, state, collected);
        DiffObjectSets(&state->sentObjects, &collected, &added, &gone, &same);
        state->SendObjectsRemoved(gone);
        state->SendObjectsAdded(added);
        state->SendObjectsChanged(same);

        std::vector<Effect*> timed;
        std::vector<Effect*> timedAdded;
        std::vector<Effect*> timedGone;
        std::vector<Effect*> timedSame;
        CollectTimedObjects(this, state, timed);
        DiffObjectSets(&state->sentTimed, &timed, &timedAdded, &timedGone, &timedSame);
        state->SendTimedRemoved(timedGone);
        state->SendTimedAdded(timedAdded);
        state->SendTimedChanged(timedSame);

        if (zoneMode == ZoneMode::Battle && IsMultiplayerClient()) {
            int& timer = state->refreshTimer;
            if (--timer <= 0 && state->danger == PlayerServer::PartyDanger::Safe) state->ResendParty();
        }
    }

    PlayerServer::State mode = state->state;
    if ((mode == PlayerServer::State::Joined || mode == PlayerServer::State::GameStateSent) && zoneLoaded != 0)
        state->SendPartyState();
    mode = state->state;
    if (mode == PlayerServer::State::Leaving) return 1;
    if (mode != PlayerServer::State::NotLoggedIn && (player->dirty & PlayerDirty::GameState) &&
        mode != PlayerServer::State::Disconnected)
        state->SendGameTime();
    return state->Flush();
}

void WorldServer::SendZone(Player* player) {
    PlayerServer* state = player->Server();
    if (state->state != PlayerServer::State::Joined) return;
    MemoryStream message;
    ByteStream* stream = &message;
    auto write = [&](const void* data, unsigned size) { stream->Write(data, size); };
    int previousTick = tick - 1;
    write(&previousTick, 4);
    write(mapName.data(), static_cast<unsigned>(mapName.size() + 1));   // the text and its zero byte
    write(mobName.data(), static_cast<unsigned>(mobName.size() + 1));
    int exitCount = static_cast<int>(std::ssize(exits));
    write(&exitCount, 4);
    for (const ZoneExit& exit : exits) exit.Write(&message);
    write(&cave, 4);

    UnitSet units;
    for (auto node = mapObjects.map.begin(); node != mapObjects.map.end() && node->second; ++node)
        if (Unit* unit = UnitOrNull(node->second)) units.Add(unit);
    units.Write(&message);

    state->SendZone(&message);
    player->connection->ItemsGiven(player->items, 0);
}
