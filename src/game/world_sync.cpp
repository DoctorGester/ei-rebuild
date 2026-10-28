// CWorldServer: keeping each player's view of the world up to date (single player too: the local
// player is a client of the server). SyncPlayer runs once per world update for every player
// SendZone sends a player the zone it has to load. See world.h and docs/notes/world.md.
#include "world.h"
#include "options.h"

#include "combat.h"
#include "core_maths.h"
#include "core_streams.h"
#include "map_object.h"
#include "object.h"
#include "player_messages.h"
#include "unit.h"
#include "world_effect.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc_slots.h"

#include "ei/x87.h"
#include "game_hashes.h"
#include "game_data.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

void DiffObjectSets(std::vector<CEngineObject*>* sent, std::vector<CEngineObject*>* current,
                               std::vector<CEngineObject*>* added, std::vector<CEngineObject*>* gone,
                               std::vector<CEngineObject*>* same);

namespace {

// Distance limits, read from the exe's memory (rule 9).
float ViewRange() { return 100.0f /*0073F91C*/; }        // objects seen around the camera
float HearingScale() { return 2.0f /*0073F910*/; }     // unit perception scale
float HearingMargin() { return 5.0f /*0073BF34*/; }    // added to it
// Non-zero in a multiplayer game seen from a spectator (no camera-range test for map objects).
int SpectatorView() { return options::TheOptions()->drawVisibleOnly_; }

constexpr int kTypeNoSync = 0x3B;            // CEngineObject::type_ of objects never sent
constexpr std::uint32_t kFlagHidden = 0x10000;   // flags_: invisible (HideObject)

// Dirty bits (CPlayer::dirty_) SyncPlayer sends.
constexpr std::uint32_t kDirtyZone = 0x01;
constexpr std::uint32_t kDirtyWeather = 0x02;
constexpr std::uint32_t kDirtyTime = 0x04;
constexpr std::uint32_t kDirtyWind = 0x08;
constexpr std::uint32_t kDirtyLeave = 0x10;
constexpr std::uint32_t kDirtyCountdowns = 0x20;

// CPlayerServer::state_.
constexpr int kModePlaying = 1;
constexpr int kModeJoining = 2;
constexpr int kModeLeaving = 3;
constexpr int kModeGone = 4;

constexpr int kKindSinglePlayer = 1;         // CWorldServer::sessionKind_
constexpr int kKindNoRangeTest = 3;

// A function-static aligned pointer array ({data, size, capacity, growBy}; the data block is freed through the
// pointer stored before it).
using StaticPtrArray = std::vector<CEngineObject*>;

// SyncPlayer's four function statics (their guard bits) and their atexit destructors.
constinit std::vector<CEngineObject*> g_collected = /*007AFF48*/ {};
constinit std::vector<CEngineObject*> g_added = /*007AFF38*/ {};
constinit std::vector<CEngineObject*> g_kept = /*007AFF28*/ {};
constinit std::vector<CEngineObject*> g_removed = /*007AFF18*/ {};
StaticPtrArray& Collected() { return g_collected; }
StaticPtrArray& Added() { return g_added; }
StaticPtrArray& Kept() { return g_kept; }
StaticPtrArray& Removed() { return g_removed; }
// The exit functions: each frees its array's block (the fields stay).
void FreeCollected() { lib::FreeStorage(g_collected); }
void FreeAdded() { lib::FreeStorage(g_added); }
void FreeKept() { lib::FreeStorage(g_kept); }
void FreeRemoved() { lib::FreeStorage(g_removed); }
std::uint8_t g_staticsGuard = /*007AFF58*/ {};
std::uint8_t& StaticsGuard() { return g_staticsGuard; }
using AtExitFunction = void(__cdecl*)();

// Compares the ids in `known` with the objects in `current`; fills `added`, `kept` and `removed`.
void DiffSets(PlayerObjectArray* known, StaticPtrArray* current, StaticPtrArray* added, StaticPtrArray* kept,
              StaticPtrArray* removed) {
    ::DiffObjectSets(known, current, added, kept, removed);
}

// The function statics' lazy construction (empty arrays, destructor registered with atexit).
void InitStatic(std::uint8_t bit, StaticPtrArray& array, AtExitFunction destructor) {
    if (StaticsGuard() & bit) return;
    StaticsGuard() |= bit;
    array.clear();
    std::atexit(destructor);
}

const float* PositionOf(CMapObject* object) { return &object->localPosition_.x; }

// Whether (x, y) lies outside the camera's view range around (cx, cy); NaN counts as inside. The squared distance
// and the squared range stay on the FPU (dy^2 + dx^2, in that order).
bool OutOfView(const float* point, const CPlayerServer* state) {
    double dx = static_cast<double>(point[0]) - state->lastPosition_.x;
    double dy = static_cast<double>(point[1]) - state->lastPosition_.y;
    double distance = LengthSquared2DUnrounded(dy, dx);
    double range = static_cast<double>(ViewRange()) * ViewRange();
    return distance >= range;
}

} // namespace

namespace {

// The map objects a player should know: around its camera (or all of them), its own units, the
// objects named in scripts, and every unit within hearing range of one of its units. `out` is the
// write cursor into Collected().
CEngineObject** CollectMapObjects(CWorldServer* world, CPlayer* player, CPlayerServer* state, CEngineObject** out) {
    if (world->sessionKind_ == kKindSinglePlayer && SpectatorView() == 0) {
        // Every map object near the camera, and the player's units anywhere; nothing else.
        for (auto node = world->mapObjects_.map.begin(); node != world->mapObjects_.map.end() && node->second; ++node) {
            CMapObject* object = node->second;
            if (object->type_ == kTypeNoSync) continue;
            if (!OutOfView(PositionOf(object), state)) {
                *out++ = object;
                continue;
            }
            if (CUnit* unit = UnitOrNull(object); unit && unit->player == player) *out++ = object;
        }
        return out;
    }

    MapObjectIdMap& source = world->sessionKind_ == kKindSinglePlayer ? world->otherMapObjects_ : world->mapObjects_;
    if (world->sessionKind_ == kKindNoRangeTest) {
        for (auto node = source.map.begin(); node != source.map.end() && node->second; ++node) {
            CEngineObject* object = node->second;
            if (object->type_ != kTypeNoSync && !(object->flags_ & kFlagHidden)) *out++ = object;
        }
    } else {
        for (auto node = source.map.begin(); node != source.map.end() && node->second; ++node) {
            CMapObject* object = node->second;
            if (object->type_ != kTypeNoSync && !OutOfView(PositionOf(object), state)) *out++ = object;
        }
    }
    if (world->sessionKind_ != kKindSinglePlayer) return out;

    // The units always sent.
    int extra = state->partyUnits_.size;
    for (int i = 0; i < extra; ++i) *out++ = state->partyUnits_.data[i].object;
    for (auto node = world->namedObjects_.map.begin(); node != world->namedObjects_.map.end() && node->second; ++node)
        *out++ = node->second;

    // The units each of the player's units can hear: max(perception * factor, minimum) * scale +
    // margin around it (the server half's movement factor 3 and statistics B0 values 0 and 2).
    auto& units = player->units_;
    for (auto node = units.begin(); node != units.end() && node->second; ++node) {
        CUnit* unit = node->second;
        mfc::CTypedPtrList<CUnit> heard;
        heard.vtable = mfc::impl::PtrListClass::Vtable();
        heard.head = nullptr;
        heard.tail = nullptr;
        heard.count = 0;
        heard.free = nullptr;
        heard.blocks = nullptr;
        heard.blockSize = 10;
        auto* controller = unit->ServerController();
        float perception = static_cast<float>(controller->moveFactors[3] * controller->stats.valuesB0[0].current);
        float minimum = controller->stats.valuesB0[2].current;
        float range = perception >= minimum ? perception : minimum;   // branch-free select
        float radius = static_cast<float>(static_cast<double>(range) * HearingScale() + HearingMargin());
        world->Sectors()->FindUnitsInRadius(&unit->localPosition_, &heard, radius);
        for (auto* entry = heard.head; entry; entry = entry->next) *out++ = entry->data;
        heard.vtable = mfc::impl::PtrListClass::Vtable();
        heard.RemoveAll();
    }
    return out;
}

// The timed objects (spells, effects) a player should know: those whose reach (a virtual, squared) covers its
// camera.
CEngineObject** CollectTimedObjects(CWorldServer* world, CPlayerServer* state, CEngineObject** out) {
    for (auto node = world->timedObjects_.map.begin(); node != world->timedObjects_.map.end() && node->second; ++node) {
        CEngineObject* object = node->second;
        auto* timed = static_cast<CEffect*>(object);
        const Vector3* position = timed->Position(&state->partyUnits_);
        double dx = static_cast<double>(position->x) - state->lastPosition_.x;
        double dy = static_cast<double>(position->y) - state->lastPosition_.y;
        float distance = static_cast<float>(LengthSquared2DUnrounded(dy, dx));
        float reach = static_cast<float>(timed->Radius());
        double reach2 = timed->Radius() * reach;
        if (reach2 > distance) *out++ = object;
    }
    return out;
}

} // namespace

int CWorldServer::SyncPlayer(CPlayer* player) {
    InitStatic(1, Collected(), FreeCollected);
    InitStatic(2, Added(), FreeAdded);
    InitStatic(4, Kept(), FreeKept);
    InitStatic(8, Removed(), FreeRemoved);

    CPlayerServer* state = player->Server();
    if (state->state_ == kModePlaying && zoneLoaded_ != 0) {
        std::uint32_t dirty = player->dirty_;
        if (dirty & kDirtyZone) {
            int sent = SendZone(player);
            if (!sent) return sent;
            state->SetDisconnect();
            state->SendItems();
            return state->Flush();
        }
        if (dirty & kDirtyWeather) {
            int strength = x87::fistp(weatherStrength_);
            if (!state->SendState70(static_cast<std::uint8_t>(weather_), static_cast<std::uint16_t>(strength))) return 0;
        }
        if (player->dirty_ & kDirtyTime) {
            float time;
            float speed;
            GetTime(&time, &speed);
            if (!state->SendState71(std::bit_cast<std::uint32_t>(time), std::bit_cast<std::uint32_t>(speed))) return 0;
        }
        if (player->dirty_ & kDirtyWind) {
            Vector3 direction;
            std::memcpy(&direction, windDirection_, sizeof(direction));
            if (!state->SendState72(std::bit_cast<std::uint32_t>(direction.x), std::bit_cast<std::uint32_t>(direction.y),
                                    std::bit_cast<std::uint32_t>(direction.z), windStrength_))
                return 0;
        }
        if (player->dirty_ & kDirtyCountdowns) state->SendState73(&countdowns_);

        // Map objects: room for seven entries per world object.
        StaticPtrArray& collected = Collected();
        collected.resize(static_cast<int>(objects_.map.size()) * 7);
        CEngineObject** end = CollectMapObjects(this, player, state, collected.data());
        collected.resize(static_cast<int>(end - Collected().data()));
        DiffSets(&state->sentObjects_, &collected, &Added(), &Kept(), &Removed());
        state->SendObjectsRemoved(&Kept());
        state->SendObjectsAdded(&Added());
        state->SendObjectsChanged(&Removed());

        collected.resize(static_cast<int>(timedObjects_.map.size()));
        end = CollectTimedObjects(this, state, collected.data());
        collected.resize(static_cast<int>(end - Collected().data()));
        DiffSets(&state->sentTimed_, &collected, &Added(), &Kept(), &Removed());
        state->SendTimedRemoved(&Kept());
        state->SendTimedAdded(&Added());
        state->SendTimedChanged(&Removed());

        if (sessionKind_ == kKindSinglePlayer && IsMultiplayerClient()) {
            int& timer = state->refreshTimer_;
            if (--timer <= 0 && state->refreshPending_ == 0) state->ResendParty();
        }
    }

    int mode = state->state_;
    if ((mode == kModePlaying || mode == kModeJoining) && zoneLoaded_ != 0) state->SendPartyState();
    mode = state->state_;
    if (mode == kModeLeaving) return 1;
    if (mode != 0 && (player->dirty_ & kDirtyLeave) && mode != kModeGone) state->SendGameTime();
    return state->Flush();
}


int CWorldServer::SendZone(CPlayer* player) {
    CMemoryStream message;

    CPlayerServer* state = player->Server();
    int result;
    if (state->state_ != kModePlaying) {
        result = 1;
    } else {
        CStream* stream = &message;
        auto write = [&](const void* data, unsigned size) { stream->Write(data, size); };
        int previousTick = tick_ - 1;
        write(&previousTick, 4);
        write(mapName_.c_str(), static_cast<unsigned>(mapName_.size() + 1));
        write(mobName_.c_str(), static_cast<unsigned>(mobName_.size() + 1));
        int exits = exits_.size;
        write(&exits, 4);
        for (int i = 0; i < exits_.size; ++i) exits_[i].Write(&message);
        write(&cave_, 4);

        UnitSet units;
        units.figured.Construct(100, 0, 0, 0);
        units.dressed.Construct(100, 0, 0, 0);
        for (auto node = mapObjects_.map.begin(); node != mapObjects_.map.end() && node->second; ++node)
            if (CUnit* unit = UnitOrNull(node->second)) units.Add(unit);
        units.Write(&message);
        units.dressed.Destroy();
        units.figured.Destroy();

        result = state->SendState02(&message);
        if (result != 0 && sendsStartPosition_ != 0) {
            result = 1;
            if (player->units_.size() != 0) {
                // The player starts at its first unit, on the ground there.
                const float* at = PositionOf(player->units_.begin()->second);
                Vector3 start;
                std::memcpy(&start, at, sizeof(start));
                start.z = static_cast<float>(RawSurfaceHeightAt(start.x, start.y));
                result = state->SendPosition04(&start, 0, sessionKind_);
            }
        }
        if (result != 0) player->connection_->ItemsGiven(&player->items_, 0);
    }
    return result;
}
