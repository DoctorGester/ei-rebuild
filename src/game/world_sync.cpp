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

#include <cstring>

namespace {

// ---- Data -------------------------------------------------------------------------------------

// Distance limits, read from the exe's memory as the original does (rule 9).
float ViewRange() { return *reinterpret_cast<const float*>(0x0073F91C); }        // objects seen around the camera
float HearingScale() { return *reinterpret_cast<const float*>(0x0073F910); }     // unit perception scale
float HearingMargin() { return *reinterpret_cast<const float*>(0x0073BF34); }    // added to it
// Non-zero in a multiplayer game seen from a spectator (no camera-range test for map objects).
int SpectatorView() { return *reinterpret_cast<const int*>(0x0079BE6C); }

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
StaticPtrArray& Collected() { return *reinterpret_cast<StaticPtrArray*>(0x007AFF48); }
StaticPtrArray& Added() { return *reinterpret_cast<StaticPtrArray*>(0x007AFF38); }
StaticPtrArray& Kept() { return *reinterpret_cast<StaticPtrArray*>(0x007AFF28); }
StaticPtrArray& Removed() { return *reinterpret_cast<StaticPtrArray*>(0x007AFF18); }
std::uint8_t& StaticsGuard() { return *reinterpret_cast<std::uint8_t*>(0x007AFF58); }
using AtExitFunction = void(__cdecl*)();
AtExitFunction CollectedDestructor() { return reinterpret_cast<AtExitFunction>(0x0058C4D0); }
AtExitFunction AddedDestructor() { return reinterpret_cast<AtExitFunction>(0x0058C4B0); }
AtExitFunction KeptDestructor() { return reinterpret_cast<AtExitFunction>(0x0058C490); }
AtExitFunction RemovedDestructor() { return reinterpret_cast<AtExitFunction>(0x0058C470); }

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------

// The game's atexit (006E727B).
// 0058C890 (RET 4). SendZone, through its address (rewritten here).
__declspec(naked) int __fastcall SendZoneAt(CWorldServer*, void*, WorldPlayer*) EI_ORIGINAL(0x0058C890)
// 0055FBD0 (RET 8). The time of day and the time speed.
// The player state's (CPlayerServer) messages and updates.
__declspec(naked) void __fastcall StateZoneSent(CPlayerServer*, void*) EI_ORIGINAL(0x0066D250)
__declspec(naked) void __fastcall StateClearUnits(CPlayerServer*, void*) EI_ORIGINAL(0x0066F080)
__declspec(naked) int __fastcall StateFlush(CPlayerServer*, void*) EI_ORIGINAL(0x0066D100)
__declspec(naked) int __fastcall StateSendWeather(CPlayerServer*, void*, int /*byte*/, int /*strength*/) EI_ORIGINAL(0x0066EC10)
__declspec(naked) int __fastcall StateSendTime(CPlayerServer*, void*, float /*time*/, float /*speed*/) EI_ORIGINAL(0x0066EC70)
__declspec(naked) int __fastcall StateSendWind(CPlayerServer*, void*, Vector3 /*direction*/, std::uint32_t /*strength*/)
    EI_ORIGINAL(0x0066ECD0)
__declspec(naked) void __fastcall StateSendCountdowns(CPlayerServer*, void*, void* /*countdowns*/) EI_ORIGINAL(0x0066F240)
__declspec(naked) void __fastcall StateObjectsKept(CPlayerServer*, void*, StaticPtrArray*) EI_ORIGINAL(0x0066DAD0)
void __fastcall StateObjectsAdded(CPlayerServer* a0, void*, StaticPtrArray* a2) { StateObjectsAddedOriginal(a0, nullptr, a2); }
__declspec(naked) void __fastcall StateObjectsRemoved(CPlayerServer*, void*, StaticPtrArray*) EI_ORIGINAL(0x0066DBA0)
__declspec(naked) void __fastcall StateTimedKept(CPlayerServer*, void*, StaticPtrArray*) EI_ORIGINAL(0x0066DF40)
__declspec(naked) void __fastcall StateTimedAdded(CPlayerServer*, void*, StaticPtrArray*) EI_ORIGINAL(0x0066DDE0)
__declspec(naked) void __fastcall StateTimedRemoved(CPlayerServer*, void*, StaticPtrArray*) EI_ORIGINAL(0x0066E020)
void __fastcall StateRefresh(CPlayerServer* a0, void*) { PlayerResendPartyOriginal(a0); }
__declspec(naked) void __fastcall StateUpdateUnits(CPlayerServer*, void*) EI_ORIGINAL(0x0066E430)
__declspec(naked) void __fastcall StateLeave(CPlayerServer*, void*) EI_ORIGINAL(0x0066F180)
__declspec(naked) int __fastcall StateSendZone(CPlayerServer*, void*, void* /*message*/) EI_ORIGINAL(0x0066EBD0)
// 0047B1F0 (RET 0xC). Compares the ids in `known` with the objects in `current`; fills `added`,
// `kept` and `removed` (ECX known, EDX current).
__declspec(naked) void __fastcall DiffObjectSets(void* /*known*/, StaticPtrArray* /*current*/, StaticPtrArray* /*added*/,
                                                 StaticPtrArray* /*kept*/, StaticPtrArray* /*removed*/) EI_ORIGINAL(0x0047B1F0)
// 005C0BD0 (RET 0xC). CSectorMap: the units within `radius` of `position` into a CPtrList.
// 0052CA10. CPtrList destructor body (RemoveAll).
inline void __fastcall PtrListDestruct(void* l, void*) { PtrListRemoveAllOriginal(l); }
// 004E58A0 (RET 4). Writes a zone exit to a stream.
__declspec(naked) void __fastcall ZoneExitWrite(ZoneExit*, void*, void* /*stream*/) EI_ORIGINAL(0x004E58A0)
// The unit set SendZone fills (world.h): its two maps' constructors and destructors (the figured
// groups 005985E0 / 005986B0, the dressed records 00598790 / 00598860), the unit added with
// 005593D0 and the set written with 00559E60.
__declspec(naked) void __fastcall FiguredGroupsConstruct(void*, void*) EI_ORIGINAL(0x005985E0)
__declspec(naked) void __fastcall FiguredGroupsDestruct(void*, void*) EI_ORIGINAL(0x005986B0)
__declspec(naked) void __fastcall UnitSetAdd(UnitSet*, void*, CEngineObject*) EI_ORIGINAL(0x005593D0)
__declspec(naked) void __fastcall UnitSetWrite(UnitSet*, void*, void* /*stream*/) EI_ORIGINAL(0x00559E60)
__declspec(naked) void __fastcall DressedMapConstruct(void*, void*) EI_ORIGINAL(0x00598790)
__declspec(naked) void __fastcall DressedMapDestruct(void*, void*) EI_ORIGINAL(0x00598860)

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
        CEngineObject* unit = node->value;
        mfc::CTypedPtrList<CEngineObject> heard;
        heard.vtable = mfc::PtrListVtable();
        heard.head = nullptr;
        heard.tail = nullptr;
        heard.count = 0;
        heard.free = nullptr;
        heard.blocks = nullptr;
        heard.blockSize = 10;
        auto* controller = static_cast<CUnitServer*>(static_cast<CUnit*>(unit)->controller);
        float perception = StoreFloat(controller->moveFactors[3] * controller->stats.valuesB0[0].current);
        float minimum = controller->stats.valuesB0[2].current;
        float range = !(perception >= minimum) ? minimum : perception;   // the original's branch-free select
        float radius = StoreFloat(static_cast<float>(static_cast<double>(range) * HearingScale() + HearingMargin()));
        SectorMapFindUnitsInRadius(world->sectorMap_, nullptr, PositionOf(unit), &heard, radius);
        for (auto* entry = heard.head; entry; entry = entry->next) *out++ = entry->data;
        heard.vtable = mfc::PtrListVtable();
        PtrListDestruct(&heard, nullptr);
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
        float distance = StoreFloat(static_cast<float>(LengthSquared2DUnrounded(dy, dx)));
        float reach = StoreFloat(static_cast<float>(TimedSlots(object)->Radius()));
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
            int sent = SendZoneAt(this, nullptr, player);
            if (!sent) return sent;
            StateZoneSent(state, nullptr);
            StateClearUnits(state, nullptr);
            return StateFlush(state, nullptr);
        }
        if (dirty & kDirtyWeather) {
            int strength = x87::fistp(weatherStrength_);
            // The original pushes EAX with the byte in AL (the rest is left from the dirty flags);
            // the callee reads the byte.
            if (!StateSendWeather(state, nullptr, static_cast<std::uint8_t>(weather_), strength)) return 0;
        }
        if (player->dirty & kDirtyTime) {
            float time;
            float speed;
            WorldTimeSettings(this, nullptr, &time, &speed);
            if (!StateSendTime(state, nullptr, time, speed)) return 0;
        }
        if (player->dirty & kDirtyWind) {
            Vector3 direction;
            std::memcpy(&direction, windDirection_, sizeof(direction));
            if (!StateSendWind(state, nullptr, direction, windStrength_)) return 0;
        }
        if (player->dirty & kDirtyCountdowns) StateSendCountdowns(state, nullptr, countdowns_);

        // Map objects: room for seven entries per world object.
        StaticPtrArray& collected = Collected();
        SetSize(collected, static_cast<int>(objects_.map.Size()) * 7);
        CEngineObject** end = CollectMapObjects(this, player, state, collected.data);
        SetSize(collected, static_cast<int>(end - Collected().data));
        DiffObjectSets(&state->sentObjects_, &collected, &Added(), &Kept(), &Removed());
        StateObjectsKept(state, nullptr, &Kept());
        StateObjectsAdded(state, nullptr, &Added());
        StateObjectsRemoved(state, nullptr, &Removed());

        // Timed objects.
        SetSize(collected, static_cast<int>(timedObjects_.map.Size()));
        end = CollectTimedObjects(this, state, collected.data);
        SetSize(collected, static_cast<int>(end - Collected().data));
        DiffObjectSets(&state->sentTimed_, &collected, &Added(), &Kept(), &Removed());
        StateTimedKept(state, nullptr, &Kept());
        StateTimedAdded(state, nullptr, &Added());
        StateTimedRemoved(state, nullptr, &Removed());

        if (sessionKind_ == kKindSinglePlayer && IsMultiplayerClient()) {
            int& timer = state->refreshTimer_;
            if (--timer <= 0 && state->refreshPending_ == 0) StateRefresh(state, nullptr);
        }
    }

    int mode = state->state_;
    if ((mode == kModePlaying || mode == kModeJoining) && zoneLoaded_ != 0) StateUpdateUnits(state, nullptr);
    mode = state->state_;
    if (mode == kModeLeaving) return 1;
    if (mode != 0 && (player->dirty & kDirtyLeave) && mode != kModeGone) StateLeave(state, nullptr);
    return StateFlush(state, nullptr);
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
        for (int i = 0; i < exitCount_; ++i) ZoneExitWrite(&exits_[i], nullptr, &message);
        write(&cave_, 4);

        // The zone's units.
        UnitSet units;
        FiguredGroupsConstruct(&units.figured, nullptr);
        DressedMapConstruct(&units.dressed, nullptr);
        for (auto* node = mapObjects_.map.First(); node && node->value; node = mapObjects_.map.After(node))
            if (node->value->GetClassId() == world::kClassUnit) UnitSetAdd(&units, nullptr, node->value);
        UnitSetWrite(&units, nullptr, &message);
        DressedMapDestruct(&units.dressed, nullptr);
        FiguredGroupsDestruct(&units.figured, nullptr);

        result = StateSendZone(state, nullptr, &message);
        if (result != 0 && sendsStartPosition_ != 0) {
            result = 1;
            if (player->units.map.Size() != 0) {
                // The player starts at its first unit, on the ground there.
                auto* first = player->units.map.First();
                const float* at = PositionOf(first ? first->value : nullptr);
                Vector3 start;
                std::memcpy(&start, at, sizeof(start));
                start.z = static_cast<float>(RawSurfaceHeightAt(start.x, start.y));
                result = PlayerSendPosition04Original(state, nullptr, &start, 0, sessionKind_);
            }
        }
        if (result != 0) reinterpret_cast<CPlayer*>(player)->connection_->ItemsGiven(&player->items, 0);
    }
    ei::SetVtable(&message, kMemoryStreamVtable);
    if (message.data_) gamecrt::operator_delete(message.data_);
    return result;
}

EI_REPLACE_METHOD(0x0058B750, CWorldServer::SyncPlayer);
EI_REPLACE_METHOD(0x0058C890, CWorldServer::SendZone);
