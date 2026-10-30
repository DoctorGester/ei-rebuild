// WorldServer: keeping each player's view of the world up to date (single player too: the local
// player is a client of the server). SyncPlayer runs once per world update for every player
// SendZone sends a player the zone it has to load. See world.h and docs/notes/world.md.
#include "combat.h"
#include "core_maths.h"
#include "core_streams.h"
#include "map_object.h"
#include "object.h"
#include "options.h"
#include "player_messages.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"

#include "lib/bytes.h"
#include "lib/containers.h"

#include "lib/float_select.h"
#include "lib/pc24.h"
#include "lib/rounding.h"

#include <bit>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

void DiffObjectSets(std::vector<EngineObject*>* sent, std::vector<EngineObject*>* current,
                               std::vector<EngineObject*>* added, std::vector<EngineObject*>* gone,
                               std::vector<EngineObject*>* same);

namespace {

// Distance limits, read from the exe's memory (rule 9).
constexpr float kViewRange = 100.0f;        // objects seen around the camera
constexpr float kHearingScale = 2.0f;     // unit perception scale
constexpr float kHearingMargin = 5.0f;    // added to it

constexpr MapObjectType kTypeNoSync = MapObjectType::MagicTrap;   // never sent

// PlayerServer::state.
constexpr int kModePlaying = 1;
constexpr int kModeJoining = 2;
constexpr int kModeLeaving = 3;
constexpr int kModeGone = 4;

constexpr int kKindSinglePlayer = 1;         // WorldServer::sessionKind
constexpr int kKindNoRangeTest = 3;

// Compares the ids in `known` with the objects in `current`; fills `added`, `kept` and `removed`.
void DiffSets(std::vector<EngineObject*>* known, std::vector<EngineObject*>* current, std::vector<EngineObject*>* added, std::vector<EngineObject*>* kept,
              std::vector<EngineObject*>* removed) {
    ::DiffObjectSets(known, current, added, kept, removed);
}

// Whether (x, y) lies outside the camera's view range around (cx, cy); NaN counts as inside. The squared distance
// is dy^2 + dx^2, in that order.
bool OutOfView(const float* point, const PlayerServer* state) {
    float dx = point[0] - state->lastPosition.x;
    float dy = point[1] - state->lastPosition.y;
    float distance = LengthSquared2DUnrounded(dy, dx);
    float range = kViewRange * kViewRange;
    return distance >= range;
}

// The map objects a player should know: around its camera (or all of them), its own units, the
// objects named in scripts, and every unit within hearing range of one of its units. `out` is the
// write cursor into the collected array.
EngineObject** CollectMapObjects(WorldServer* world, Player* player, PlayerServer* state, EngineObject** out) {
    if (world->sessionKind == kKindSinglePlayer && options::g_options.drawVisibleOnly == 0) {
        // Every map object near the camera, and the player's units anywhere; nothing else.
        for (auto node = world->mapObjects.map.begin(); node != world->mapObjects.map.end() && node->second; ++node) {
            MapObject* object = node->second;
            if (object->MapType() == kTypeNoSync) continue;
            if (!OutOfView(&object->localPosition.x, state)) {
                *out++ = object;
                continue;
            }
            if (Unit* unit = UnitOrNull(object); unit && unit->player == player) *out++ = object;
        }
        return out;
    }

    MapObjectIdMap& source = world->sessionKind == kKindSinglePlayer ? world->otherMapObjects : world->mapObjects;
    if (world->sessionKind == kKindNoRangeTest) {
        for (auto node = source.map.begin(); node != source.map.end() && node->second; ++node) {
            MapObject* object = node->second;
            if (object->MapType() != kTypeNoSync && !(object->flags & MapObjectFlag::Invisible)) *out++ = object;
        }
    } else {
        for (auto node = source.map.begin(); node != source.map.end() && node->second; ++node) {
            MapObject* object = node->second;
            if (object->MapType() != kTypeNoSync && !OutOfView(&object->localPosition.x, state)) *out++ = object;
        }
    }
    if (world->sessionKind != kKindSinglePlayer) return out;

    // The units always sent.
    for (const ScriptObjectRef& unit : state->partyUnits) *out++ = unit.object;
    for (auto node = world->namedObjects.map.begin(); node != world->namedObjects.map.end() && node->second; ++node)
        *out++ = node->second;

    // The units each of the player's units can hear: max(perception * factor, minimum) * scale +
    // margin around it (the server half's movement factor 3 and statistics B0 values 0 and 2).
    auto& units = player->units;
    for (auto node = units.begin(); node != units.end() && node->second; ++node) {
        Unit* unit = node->second;
        auto* controller = unit->ServerController();
        float perception = controller->moveFactors[3] * controller->stats.perception[0].current;
        float minimum = controller->stats.perception[2].current;
        float range = MaxKeepB(perception, minimum);   // branch-free select
        float radius = range * kHearingScale + kHearingMargin;
        for (Unit* heard : world->aiMap.sectorMap.FindUnitsInRadius(unit->localPosition, radius)) *out++ = heard;
    }
    return out;
}

// The timed objects (spells, effects) a player should know: those whose reach (a virtual, squared) covers its
// camera.
EngineObject** CollectTimedObjects(WorldServer* world, PlayerServer* state, EngineObject** out) {
    for (auto node = world->timedObjects.map.begin(); node != world->timedObjects.map.end() && node->second; ++node) {
        EngineObject* object = node->second;
        auto* timed = static_cast<Effect*>(object);
        const Vector3* position = timed->Position(&state->partyUnits);
        float dx = position->x - state->lastPosition.x;
        float dy = position->y - state->lastPosition.y;
        float distance = LengthSquared2DUnrounded(dy, dx);
        float reach = static_cast<float>(timed->Radius());
        float reach2 = pc24::Mul(timed->Radius(), reach);
        if (reach2 > distance) *out++ = object;
    }
    return out;
}

} // namespace

int WorldServer::SyncPlayer(Player* player) {
    PlayerServer* state = player->Server();
    if (state->state == kModePlaying && zoneLoaded != 0) {
        std::uint32_t dirty = player->dirty;
        if (dirty & PlayerDirty::Zone) {
            int sent = SendZone(player);
            if (!sent) return sent;
            state->SetDisconnect();
            state->SendItems();
            return state->Flush();
        }
        if (dirty & PlayerDirty::Weather) {
            int strength = lib::RoundEven(weatherStrength);
            if (!state->SendState70(static_cast<std::uint8_t>(weather), static_cast<std::uint16_t>(strength))) return 0;
        }
        if (player->dirty & PlayerDirty::Time) {
            float time;
            float speed;
            GetTime(&time, &speed);
            if (!state->SendState71(std::bit_cast<std::uint32_t>(time), std::bit_cast<std::uint32_t>(speed))) return 0;
        }
        if (player->dirty & PlayerDirty::Wind) {
            Vector3 direction;
            lib::LoadBytes(direction, windDirection);
            if (!state->SendState72(std::bit_cast<std::uint32_t>(direction.x), std::bit_cast<std::uint32_t>(direction.y),
                                    std::bit_cast<std::uint32_t>(direction.z), windStrength))
                return 0;
        }
        if (player->dirty & PlayerDirty::Countdowns) state->SendState73(&countdowns);

        // Map objects: room for seven entries per world object.
        std::vector<EngineObject*> collected;
        std::vector<EngineObject*> added;
        std::vector<EngineObject*> kept;
        std::vector<EngineObject*> removed;
        collected.resize(static_cast<int>(objects.map.size()) * 7);
        EngineObject** end = CollectMapObjects(this, player, state, collected.data());
        collected.resize(static_cast<int>(end - collected.data()));
        DiffSets(&state->sentObjects, &collected, &added, &kept, &removed);
        state->SendObjectsRemoved(&kept);
        state->SendObjectsAdded(&added);
        state->SendObjectsChanged(&removed);

        collected.resize(static_cast<int>(timedObjects.map.size()));
        end = CollectTimedObjects(this, state, collected.data());
        collected.resize(static_cast<int>(end - collected.data()));
        DiffSets(&state->sentTimed, &collected, &added, &kept, &removed);
        state->SendTimedRemoved(&kept);
        state->SendTimedAdded(&added);
        state->SendTimedChanged(&removed);

        if (sessionKind == kKindSinglePlayer && IsMultiplayerClient()) {
            int& timer = state->refreshTimer;
            if (--timer <= 0 && state->danger == PlayerServer::PartyDanger::Safe) state->ResendParty();
        }
    }

    int mode = state->state;
    if ((mode == kModePlaying || mode == kModeJoining) && zoneLoaded != 0) state->SendPartyState();
    mode = state->state;
    if (mode == kModeLeaving) return 1;
    if (mode != 0 && (player->dirty & PlayerDirty::GameState) && mode != kModeGone) state->SendGameTime();
    return state->Flush();
}

int WorldServer::SendZone(Player* player) {
    MemoryStream message;

    PlayerServer* state = player->Server();
    int result;
    if (state->state != kModePlaying) {
        result = 1;
    } else {
        ByteStream* stream = &message;
        auto write = [&](const void* data, unsigned size) { stream->Write(data, size); };
        int previousTick = tick - 1;
        write(&previousTick, 4);
        write(mapName.c_str(), static_cast<unsigned>(mapName.size() + 1));
        write(mobName.c_str(), static_cast<unsigned>(mobName.size() + 1));
        int exitCount = static_cast<int>(std::ssize(exits));
        write(&exitCount, 4);
        for (const ZoneExit& exit : exits) exit.Write(&message);
        write(&cave, 4);

        UnitSet units;
        for (auto node = mapObjects.map.begin(); node != mapObjects.map.end() && node->second; ++node)
            if (Unit* unit = UnitOrNull(node->second)) units.Add(unit);
        units.Write(&message);

        result = state->SendState02(&message);
        if (result != 0) player->connection->ItemsGiven(&player->items, 0);
    }
    return result;
}
