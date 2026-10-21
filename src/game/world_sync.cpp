// CWorldServer: keeping each player's view of the world up to date (single player too: the local
// player is a client of the server). SyncPlayer runs once per world update for every player
// (0058DFF0); SendZone sends a player the zone it has to load. See world.h and docs/notes/world.md.
#include "world.h"

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

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_gameplay.h"
#include "originals_world.h"
#include "game_hashes.h"
#include "game_data.h"

#include <cstring>

void __fastcall DiffObjectSets(lib::AlignedArray<std::uint32_t>* sent, lib::AlignedArray<std::uint32_t>* current,
                               lib::AlignedArray<std::uint32_t>* added, lib::AlignedArray<std::uint32_t>* gone,
                               lib::AlignedArray<std::uint32_t>* same);   // 0047B1F0 (world_object_sets.cpp)

namespace {

// ---- Data -------------------------------------------------------------------------------------

// Distance limits, read from the exe's memory as the original does (rule 9).
float ViewRange() { return 100.0f /*0073F91C*/; }        // objects seen around the camera
float HearingScale() { return 2.0f /*0073F910*/; }     // unit perception scale
float HearingMargin() { return 5.0f /*0073BF34*/; }    // added to it
// Non-zero in a multiplayer game seen from a spectator (no camera-range test for map objects).
int SpectatorView() { return ei::GameData<const int>(0x0079BE6C); }

constexpr int kTypeNoSync = 0x3B;            // CEngineObject::type_ of objects never sent
constexpr std::uint32_t kFlagHidden = 0x10000;   // flags_: invisible (HideObject)

// Dirty bits (WorldPlayer::dirty) SyncPlayer sends.
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

// A function-static aligned pointer array of the original ({data, size, capacity, growBy}; the
// data block from 0047B940, freed through the pointer stored before it).
using StaticPtrArray = lib::AlignedArray<CEngineObject*>;

// SyncPlayer's four function statics (their guard bits at 007AFF58) and their atexit destructors.
StaticPtrArray& Collected() { return ei::GameData<StaticPtrArray>(0x007AFF48); }
StaticPtrArray& Added() { return ei::GameData<StaticPtrArray>(0x007AFF38); }
StaticPtrArray& Kept() { return ei::GameData<StaticPtrArray>(0x007AFF28); }
StaticPtrArray& Removed() { return ei::GameData<StaticPtrArray>(0x007AFF18); }
std::uint8_t& StaticsGuard() { return ei::GameData<std::uint8_t>(0x007AFF58); }
using AtExitFunction = void(__cdecl*)();
AtExitFunction CollectedDestructor() { return reinterpret_cast<AtExitFunction>(0x0058C4D0); }
AtExitFunction AddedDestructor() { return reinterpret_cast<AtExitFunction>(0x0058C4B0); }
AtExitFunction KeptDestructor() { return reinterpret_cast<AtExitFunction>(0x0058C490); }
AtExitFunction RemovedDestructor() { return reinterpret_cast<AtExitFunction>(0x0058C470); }

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------

// The game's atexit (006E727B).
// 0055FBD0 (RET 8). The time of day and the time speed.
// The player state's (CPlayerServer) messages and updates.
// 0047B1F0 (world_object_sets.cpp). Compares the ids in `known` with the objects in `current`;
// fills `added`, `kept` and `removed`.
void DiffSets(void* known, StaticPtrArray* current, StaticPtrArray* added, StaticPtrArray* kept, StaticPtrArray* removed) {
    using Set = lib::AlignedArray<std::uint32_t>;
    ::DiffObjectSets(static_cast<Set*>(known), reinterpret_cast<Set*>(current), reinterpret_cast<Set*>(added),
                     reinterpret_cast<Set*>(kept), reinterpret_cast<Set*>(removed));
}
// The unit set SendZone fills (world.h): its two maps' constructors and destructors (the figured
// groups 005985E0 / 005986B0, the dressed records 00598790 / 00598860: gamehash's tables), the unit
// added with 005593D0 and the set written with 00559E60.

// The function statics' lazy construction (empty arrays, destructor registered with atexit).
void InitStatic(std::uint8_t bit, StaticPtrArray& array, AtExitFunction destructor) {
    if (StaticsGuard() & bit) return;
    array.data = nullptr;
    StaticsGuard() |= bit;
    array.growBy = 0;
    array.capacity = 0;
    array.size = 0;
    GameAtExit(destructor);
}

// The inlined SetSize of the aligned arrays (grow by growBy, or size / 8 clamped to 4..1024).
void SetSize(StaticPtrArray& a, int n) {
    if (n == 0) {
        if (a.data) {
            gamecrt::operator_delete(reinterpret_cast<void**>(a.data)[-1]);
            a.data = nullptr;
        }
        a.capacity = 0;
        a.size = 0;
    } else if (!a.data) {
        a.data = reinterpret_cast<CEngineObject**>(AlignedPointerArrayAlloc(n));
        a.capacity = n;
        a.size = n;
    } else if (n <= a.capacity) {
        a.size = n;
    } else {
        int grow = a.growBy;
        if (grow == 0) {
            grow = a.size / 8;
            if (grow < 4) grow = 4;
            else if (grow > 0x400) grow = 0x400;
        }
        int capacity = a.capacity + grow;
        if (n >= capacity) capacity = n;
        auto** fresh = reinterpret_cast<CEngineObject**>(AlignedPointerArrayAlloc(capacity));
        std::memcpy(fresh, a.data, static_cast<std::size_t>(a.size) * 4);
        gamecrt::operator_delete(reinterpret_cast<void**>(a.data)[-1]);
        a.data = fresh;
        a.size = n;
        a.capacity = capacity;
    }
}

CMapObject* MapObject(CEngineObject* object) { return static_cast<CMapObject*>(object); }
const float* PositionOf(CEngineObject* object) { return &MapObject(object)->position.x; }

// Whether (x, y) lies outside the camera's view range around (cx, cy); NaN counts as inside. The
// squared distance and the squared range stay on the FPU (dy^2 + dx^2, as the original adds).
bool OutOfView(const float* point, const CPlayerServer* state) {
    double dx = static_cast<double>(point[0]) - state->lastPosition_.x;
    double dy = static_cast<double>(point[1]) - state->lastPosition_.y;
    double distance = LengthSquared2DUnrounded(dy, dx);
    double range = static_cast<double>(ViewRange()) * ViewRange();
    return distance >= range;
}

} // namespace

// ---- SyncPlayer -------------------------------------------------------------------------------

namespace {

// The map objects a player should know: around its camera (or all of them), its own units, the
// objects named in scripts, and every unit within hearing range of one of its units. `out` is the
// write cursor into Collected().
CEngineObject** CollectMapObjects(CWorldServer* world, WorldPlayer* player, CPlayerServer* state, CEngineObject** out) {
    if (world->sessionKind_ == kKindSinglePlayer && SpectatorView() == 0) {
        // Every map object near the camera, and the player's units anywhere; nothing else.
        for (auto* node = world->mapObjects_.map.First(); node && node->value; node = world->mapObjects_.map.After(node)) {
            CEngineObject* object = node->value;
            if (object->type_ == kTypeNoSync) continue;
            if (!OutOfView(PositionOf(object), state) ||
                (object->GetClassId() == world::kClassUnit &&
                 reinterpret_cast<WorldPlayer*>(static_cast<CUnit*>(object)->player) == player))
                *out++ = object;
        }
        return out;
    }

    ObjectIdMap& source = world->sessionKind_ == kKindSinglePlayer ? world->otherMapObjects_ : world->mapObjects_;
    if (world->sessionKind_ == kKindNoRangeTest) {
        for (auto* node = source.map.First(); node && node->value; node = source.map.After(node)) {
            CEngineObject* object = node->value;
            if (object->type_ != kTypeNoSync && !(object->flags_ & kFlagHidden)) *out++ = object;
        }
    } else {
        for (auto* node = source.map.First(); node && node->value; node = source.map.After(node)) {
            CEngineObject* object = node->value;
            if (object->type_ != kTypeNoSync && !OutOfView(PositionOf(object), state)) *out++ = object;
        }
    }
    if (world->sessionKind_ != kKindSinglePlayer) return out;

    // The units always sent.
    int extra = state->partyUnits_.size;
    for (int i = 0; i < extra; ++i) *out++ = state->partyUnits_.data[i].object;
    for (auto* node = world->namedObjects_.map.First(); node && node->value; node = world->namedObjects_.map.After(node))
        *out++ = node->value;

    // The units each of the player's units can hear: max(perception * factor, minimum) * scale +
    // margin around it (the server half's movement factor 3 and statistics B0 values 0 and 2).
    auto& units = player->units.map;
    for (auto* node = units.First(); node && node->value; node = units.After(node)) {
        CUnit* unit = node->value;
        mfc::CTypedPtrList<CUnit> heard;
        heard.vtable = mfc::PtrListVtable();
        heard.head = nullptr;
        heard.tail = nullptr;
        heard.count = 0;
        heard.free = nullptr;
        heard.blocks = nullptr;
        heard.blockSize = 10;
        auto* controller = static_cast<CUnitServer*>(unit->controller);
        float perception = static_cast<float>(controller->moveFactors[3] * controller->stats.valuesB0[0].current);
        float minimum = controller->stats.valuesB0[2].current;
        float range = perception >= minimum ? perception : minimum;   // the original's branch-free select
        float radius = static_cast<float>(static_cast<double>(range) * HearingScale() + HearingMargin());
        world->Sectors()->FindUnitsInRadius(&MapObject(unit)->position, &heard, radius);
        for (auto* entry = heard.head; entry; entry = entry->next) *out++ = entry->data;
        heard.vtable = mfc::PtrListVtable();
        heard.RemoveAll();
    }
    return out;
}

// The timed objects (spells, effects) a player should know: those whose reach (slot 19 squared)
// covers its camera.
CEngineObject** CollectTimedObjects(CWorldServer* world, CPlayerServer* state, CEngineObject** out) {
    for (auto* node = world->timedObjects_.map.First(); node && node->value; node = world->timedObjects_.map.After(node)) {
        CEngineObject* object = node->value;
        const Vector3* position = TimedSlots(object)->Position(&state->partyUnits_);
        double dx = static_cast<double>(position->x) - state->lastPosition_.x;
        double dy = static_cast<double>(position->y) - state->lastPosition_.y;
        float distance = static_cast<float>(LengthSquared2DUnrounded(dy, dx));
        float reach = static_cast<float>(TimedSlots(object)->Radius());
        double reach2 = TimedSlots(object)->Radius() * reach;
        if (reach2 > distance) *out++ = object;
    }
    return out;
}

} // namespace

int CWorldServer::SyncPlayer(WorldPlayer* player) {
    InitStatic(1, Collected(), CollectedDestructor());
    InitStatic(2, Added(), AddedDestructor());
    InitStatic(4, Kept(), KeptDestructor());
    InitStatic(8, Removed(), RemovedDestructor());

    CPlayerServer* state = player->server;
    if (state->state_ == kModePlaying && zoneLoaded_ != 0) {
        std::uint32_t dirty = player->dirty;
        if (dirty & kDirtyZone) {
            int sent = SendZone(player);
            if (!sent) return sent;
            state->SetDisconnect();
            state->SendItems();
            return state->Flush();
        }
        if (dirty & kDirtyWeather) {
            int strength = x87::fistp(weatherStrength_);
            // The original pushes EAX with the byte in AL (the rest is left from the dirty flags);
            // the callee reads the byte.
            if (!state->SendState70(static_cast<std::uint8_t>(weather_), static_cast<std::uint16_t>(strength))) return 0;
        }
        if (player->dirty & kDirtyTime) {
            float time;
            float speed;
            GetTime(&time, &speed);
            if (!state->SendState71(std::bit_cast<std::uint32_t>(time), std::bit_cast<std::uint32_t>(speed))) return 0;
        }
        if (player->dirty & kDirtyWind) {
            Vector3 direction;
            std::memcpy(&direction, windDirection_, sizeof(direction));
            if (!state->SendState72(std::bit_cast<std::uint32_t>(direction.x), std::bit_cast<std::uint32_t>(direction.y),
                                    std::bit_cast<std::uint32_t>(direction.z), windStrength_))
                return 0;
        }
        if (player->dirty & kDirtyCountdowns) state->SendState73(&countdowns_);

        // Map objects: room for seven entries per world object.
        StaticPtrArray& collected = Collected();
        SetSize(collected, static_cast<int>(objects_.map.Size()) * 7);
        CEngineObject** end = CollectMapObjects(this, player, state, collected.data);
        SetSize(collected, static_cast<int>(end - Collected().data));
        DiffSets(&state->sentObjects_, &collected, &Added(), &Kept(), &Removed());
        state->SendObjectsRemoved(&Kept());
        state->SendObjectsAdded(&Added());
        state->SendObjectsChanged(&Removed());

        // Timed objects.
        SetSize(collected, static_cast<int>(timedObjects_.map.Size()));
        end = CollectTimedObjects(this, state, collected.data);
        SetSize(collected, static_cast<int>(end - Collected().data));
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
    if (mode != 0 && (player->dirty & kDirtyLeave) && mode != kModeGone) state->SendGameTime();
    return state->Flush();
}

// ---- SendZone ---------------------------------------------------------------------------------

int CWorldServer::SendZone(WorldPlayer* player) {
    CMemoryStream message;
    std::memset(&message, 0, sizeof(message));
    message.version_ = 2000000000;
    message.ok_ = 1;
    ei::SetVtable(&message, kMemoryStreamVtable);
    message.data_ = nullptr;
    message.Reset();

    CPlayerServer* state = player->server;
    int result;
    if (state->state_ != kModePlaying) {
        result = 1;
    } else {
        CStream* stream = &message;
        auto write = [&](const void* data, unsigned size) { stream->Write(data, size); };
        int previousTick = tick_ - 1;
        write(&previousTick, 4);
        write(mapName_, static_cast<unsigned>(std::strlen(mapName_) + 1));
        write(mobName_, static_cast<unsigned>(std::strlen(mobName_) + 1));
        int exits = exitCount_;
        write(&exits, 4);
        for (int i = 0; i < exitCount_; ++i) exits_[i].Write(&message);
        write(&cave_, 4);

        // The zone's units.
        UnitSet units;
        units.figured.Construct(100);   // 005985E0
        units.dressed.Construct(100);   // 00598790
        for (auto* node = mapObjects_.map.First(); node && node->value; node = mapObjects_.map.After(node))
            if (node->value->GetClassId() == world::kClassUnit) units.Add(static_cast<CUnit*>(node->value));
        units.Write(&message);
        gamehash::DressedAbi::Destroy(reinterpret_cast<gamehash::DressedTable*>(&units.dressed));
        gamehash::FiguredGroupsAbi::Destroy(reinterpret_cast<gamehash::FiguredGroupsTable*>(&units.figured));

        result = state->SendState02(&message);   // 0066EBD0
        if (result != 0 && sendsStartPosition_ != 0) {
            result = 1;
            if (player->units.map.Size() != 0) {
                // The player starts at its first unit, on the ground there.
                auto* first = player->units.map.First();
                const float* at = PositionOf(first ? first->value : nullptr);
                Vector3 start;
                std::memcpy(&start, at, sizeof(start));
                start.z = static_cast<float>(RawSurfaceHeightAt(start.x, start.y));
                result = state->SendPosition04(&start, 0, sessionKind_);
            }
        }
        if (result != 0) reinterpret_cast<CPlayer*>(player)->connection_->ItemsGiven(&player->items, 0);
    }
    ei::SetVtable(&message, kMemoryStreamVtable);
    if (message.data_) gamecrt::operator_delete(message.data_);
    return result;
}

EI_REPLACE_METHOD(0x0058B750, CWorldServer::SyncPlayer);
EI_REWRITTEN(0x0058C890, CWorldServer::SendZone);
