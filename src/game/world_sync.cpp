// CWorldServer: keeping each player's view of the world up to date (single player too: the local
// player is a client of the server). SyncPlayer runs once per world update for every player
// (0058DFF0); SendZone sends a player the zone it has to load. See world.h and docs/notes/world.md
// (seventh round).
#include "world.h"

#include "core_maths.h"
#include "core_streams.h"
#include "object.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_gameplay.h"

#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

// ---- Data -------------------------------------------------------------------------------------

// Distance limits, read from the exe's memory as the original does (rule 9).
float ViewRange() { return *reinterpret_cast<const float*>(0x0073F91C); }        // objects seen around the camera
float HearingScale() { return *reinterpret_cast<const float*>(0x0073F910); }     // unit perception scale
float HearingMargin() { return *reinterpret_cast<const float*>(0x0073BF34); }    // added to it
// Non-zero in a multiplayer game seen from a spectator (no camera-range test for map objects).
int SpectatorView() { return *reinterpret_cast<const int*>(0x0079BE6C); }

constexpr int kTypeNoSync = 0x3B;            // CEngineObject::type_ of objects never sent
constexpr std::uint32_t kFlagHidden = 0x10000;   // flags_: invisible (HideObject)

// The player's state object (WorldPlayer + 0x20, a CPlayerServer).
constexpr int kStateMode = 0x20;             // 0 none, 1 playing, 2, 3 leaving, 4
constexpr int kStateCameraX = 0x38;
constexpr int kStateCameraY = 0x3C;
constexpr int kStateSentObjects = 0x50D8;    // ids of the map objects the player knows
constexpr int kStateSentTimed = 0x50E8;      // ... of the timed objects
constexpr int kStateTimedContext = 0x510C;   // handed to the timed objects' slot 20
constexpr int kStateExtra = 0x5110;          // objects always sent (pointer array, count at +0x5114)
constexpr int kStateExtraCount = 0x5114;
constexpr int kStateRefreshPending = 0x5120;
constexpr int kStateRefreshTimer = 0x5124;

// WorldPlayer fields.
constexpr int kPlayerState = 0x20;
constexpr int kPlayerUnits = 0x40;           // hash_map<id, unit>: the player's units
constexpr int kPlayerUnitCount = 0x50;
constexpr int kPlayerObjects = 0xE0;

// Dirty bits (WorldPlayer::dirty) SyncPlayer sends.
constexpr std::uint32_t kDirtyZone = 0x01;
constexpr std::uint32_t kDirtyWeather = 0x02;
constexpr std::uint32_t kDirtyTime = 0x04;
constexpr std::uint32_t kDirtyWind = 0x08;
constexpr std::uint32_t kDirtyLeave = 0x10;
constexpr std::uint32_t kDirtyCountdowns = 0x20;

constexpr int kModePlaying = 1;
constexpr int kModeJoining = 2;
constexpr int kModeLeaving = 3;
constexpr int kModeGone = 4;

constexpr int kKindSinglePlayer = 1;         // CWorldServer::unknown12D64_
constexpr int kKindNoRangeTest = 3;

// A function-static aligned pointer array of the original ({data, size, capacity, growBy}; the
// data block from 0047B940, freed through the pointer stored before it).
struct StaticPtrArray {
    CEngineObject** data;
    int size;
    int capacity;
    int growBy;
};
static_assert(sizeof(StaticPtrArray) == 0x10);

// SyncPlayer's four function statics (their guard bits at 007AFF58) and their atexit destructors.
StaticPtrArray& Collected() { return *reinterpret_cast<StaticPtrArray*>(0x007AFF48); }
StaticPtrArray& Added() { return *reinterpret_cast<StaticPtrArray*>(0x007AFF38); }
StaticPtrArray& Kept() { return *reinterpret_cast<StaticPtrArray*>(0x007AFF28); }
StaticPtrArray& Removed() { return *reinterpret_cast<StaticPtrArray*>(0x007AFF18); }
std::uint8_t& StaticsGuard() { return *reinterpret_cast<std::uint8_t*>(0x007AFF58); }
constexpr std::uintptr_t kCollectedDestructor = 0x0058C4D0;
constexpr std::uintptr_t kAddedDestructor = 0x0058C4B0;
constexpr std::uintptr_t kKeptDestructor = 0x0058C490;
constexpr std::uintptr_t kRemovedDestructor = 0x0058C470;

constexpr std::uintptr_t kPtrListVtable = 0x0073D454;   // CPtrList of the units within hearing range

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------

// The game's atexit (006E727B).
// 0058C890 (RET 4). SendZone, through its address (rewritten here).
__declspec(naked) int __fastcall SendZoneAt(CWorldServer*, void*, WorldPlayer*) EI_ORIGINAL(0x0058C890)
// 0055FBD0 (RET 8). The time of day and the time speed.
// The player state's (CPlayerServer) messages and updates.
__declspec(naked) void __fastcall StateZoneSent(void*, void*) EI_ORIGINAL(0x0066D250)
__declspec(naked) void __fastcall StateClearUnits(void*, void*) EI_ORIGINAL(0x0066F080)
__declspec(naked) int __fastcall StateFlush(void*, void*) EI_ORIGINAL(0x0066D100)
__declspec(naked) int __fastcall StateSendWeather(void*, void*, int /*byte*/, int /*strength*/) EI_ORIGINAL(0x0066EC10)
__declspec(naked) int __fastcall StateSendTime(void*, void*, float /*time*/, float /*speed*/) EI_ORIGINAL(0x0066EC70)
__declspec(naked) int __fastcall StateSendWind(void*, void*, Vector3 /*direction*/, std::uint32_t /*strength*/)
    EI_ORIGINAL(0x0066ECD0)
__declspec(naked) void __fastcall StateSendCountdowns(void*, void*, void* /*countdowns*/) EI_ORIGINAL(0x0066F240)
__declspec(naked) void __fastcall StateObjectsKept(void*, void*, StaticPtrArray*) EI_ORIGINAL(0x0066DAD0)
__declspec(naked) void __fastcall StateObjectsAdded(void*, void*, StaticPtrArray*) EI_ORIGINAL(0x0066D7F0)
__declspec(naked) void __fastcall StateObjectsRemoved(void*, void*, StaticPtrArray*) EI_ORIGINAL(0x0066DBA0)
__declspec(naked) void __fastcall StateTimedKept(void*, void*, StaticPtrArray*) EI_ORIGINAL(0x0066DF40)
__declspec(naked) void __fastcall StateTimedAdded(void*, void*, StaticPtrArray*) EI_ORIGINAL(0x0066DDE0)
__declspec(naked) void __fastcall StateTimedRemoved(void*, void*, StaticPtrArray*) EI_ORIGINAL(0x0066E020)
void __fastcall StateRefresh(void* a0, void*) { PlayerResendPartyOriginal(a0); }
__declspec(naked) void __fastcall StateUpdateUnits(void*, void*) EI_ORIGINAL(0x0066E430)
__declspec(naked) void __fastcall StateLeave(void*, void*) EI_ORIGINAL(0x0066F180)
__declspec(naked) int __fastcall StateSendZone(void*, void*, void* /*message*/) EI_ORIGINAL(0x0066EBD0)
__declspec(naked) int __fastcall StateSendStart(void*, void*, const Vector3* /*position*/, int /*zero*/, int /*kind*/)
    EI_ORIGINAL(0x0066ED30)
// 0047B1F0 (RET 0xC). Compares the ids in `known` with the objects in `current`; fills `added`,
// `kept` and `removed` (ECX known, EDX current).
__declspec(naked) void __fastcall DiffObjectSets(void* /*known*/, StaticPtrArray* /*current*/, StaticPtrArray* /*added*/,
                                                 StaticPtrArray* /*kept*/, StaticPtrArray* /*removed*/) EI_ORIGINAL(0x0047B1F0)
// 005C0BD0 (RET 0xC). CSectorMap: the units within `radius` of `position` into a CPtrList.
// 0052CA10. CPtrList destructor body (RemoveAll).
inline void __fastcall PtrListDestruct(void* l, void*) { PtrListRemoveAllOriginal(l); }
// 004E58A0 (RET 4). Writes a zone exit to a stream.
__declspec(naked) void __fastcall ZoneExitWrite(ZoneExit*, void*, void* /*stream*/) EI_ORIGINAL(0x004E58A0)
// The unit set SendZone fills: an STLport hash_map (0x14 bytes, constructor 005985E0, destructor
// 005986B0), the unit added with 005593D0 and written with 00559E60. A second map of the same kind
// (00598790 / 00598860) is built right after it and destroyed unused.
__declspec(naked) void __fastcall UnitSetConstruct(void*, void*) EI_ORIGINAL(0x005985E0)
__declspec(naked) void __fastcall UnitSetDestruct(void*, void*) EI_ORIGINAL(0x005986B0)
__declspec(naked) void __fastcall UnitSetAdd(void*, void*, CEngineObject*) EI_ORIGINAL(0x005593D0)
__declspec(naked) void __fastcall UnitSetWrite(void*, void*, void* /*stream*/) EI_ORIGINAL(0x00559E60)
__declspec(naked) void __fastcall SecondSetConstruct(void*, void*) EI_ORIGINAL(0x00598790)
__declspec(naked) void __fastcall SecondSetDestruct(void*, void*) EI_ORIGINAL(0x00598860)

using UnitMap = stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash>;

// The function statics' lazy construction (empty arrays, destructor registered with atexit).
void InitStatic(std::uint8_t bit, StaticPtrArray& array, std::uintptr_t destructor) {
    if (StaticsGuard() & bit) return;
    array.data = nullptr;
    StaticsGuard() |= bit;
    array.growBy = 0;
    array.capacity = 0;
    array.size = 0;
    GameAtExit(reinterpret_cast<void(__cdecl*)()>(destructor));
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

// Whether (x, y) lies outside the camera's view range around (cx, cy); NaN counts as inside. The
// squared distance and the squared range stay on the FPU (dy^2 + dx^2, as the original adds).
bool OutOfView(const float* point, const void* state) {
    double dx = static_cast<double>(point[0]) - At<float>(const_cast<void*>(state), kStateCameraX);
    double dy = static_cast<double>(point[1]) - At<float>(const_cast<void*>(state), kStateCameraY);
    double distance = dy * dy + dx * dx;
    double range = static_cast<double>(ViewRange()) * ViewRange();
    return distance >= range;
}

} // namespace

// ---- SyncPlayer -------------------------------------------------------------------------------

namespace {

// The map objects a player should know: around its camera (or all of them), its own units, the
// objects named in scripts, and every unit within hearing range of one of its units. `out` is the
// write cursor into Collected().
CEngineObject** CollectMapObjects(CWorldServer* world, WorldPlayer* player, void* state, CEngineObject** out) {
    if (world->unknown12D64_ == kKindSinglePlayer && SpectatorView() == 0) {
        // Every map object near the camera, and the player's units anywhere; nothing else.
        for (auto* node = world->mapObjects_.map.First(); node && node->value; node = world->mapObjects_.map.After(node)) {
            CEngineObject* object = node->value;
            if (object->type_ == kTypeNoSync) continue;
            if (!OutOfView(&At<float>(object, 0x1C), state) ||
                (object->GetClassId() == world::kClassUnit && At<WorldPlayer*>(object, 0x24C) == player))
                *out++ = object;
        }
        return out;
    }

    ObjectIdMap& source = world->unknown12D64_ == kKindSinglePlayer ? world->otherMapObjects_ : world->mapObjects_;
    if (world->unknown12D64_ == kKindNoRangeTest) {
        for (auto* node = source.map.First(); node && node->value; node = source.map.After(node)) {
            CEngineObject* object = node->value;
            if (object->type_ != kTypeNoSync && !(object->flags_ & kFlagHidden)) *out++ = object;
        }
    } else {
        for (auto* node = source.map.First(); node && node->value; node = source.map.After(node)) {
            CEngineObject* object = node->value;
            if (object->type_ != kTypeNoSync && !OutOfView(&At<float>(object, 0x1C), state)) *out++ = object;
        }
    }
    if (world->unknown12D64_ != kKindSinglePlayer) return out;

    int extra = At<int>(state, kStateExtraCount);
    for (int i = 0; i < extra; ++i) *out++ = At<CEngineObject**>(state, kStateExtra)[i];
    for (auto* node = world->namedObjects_.map.First(); node && node->value; node = world->namedObjects_.map.After(node))
        *out++ = node->value;

    // The units each of the player's units can hear: max(perception * factor, minimum) * scale +
    // margin around it.
    auto& units = At<UnitMap>(player, kPlayerUnits);
    for (auto* node = units.First(); node && node->value; node = units.After(node)) {
        CEngineObject* unit = node->value;
        mfc::CTypedPtrList<CEngineObject> heard;
        heard.vtable = reinterpret_cast<void*>(kPtrListVtable);
        heard.head = nullptr;
        heard.tail = nullptr;
        heard.count = 0;
        heard.free = nullptr;
        heard.blocks = nullptr;
        heard.blockSize = 10;
        auto* controller = At<std::uint8_t*>(unit, 0x240);
        float perception = StoreFloat(At<float>(controller, 0x904) * At<float>(controller, 0x294));
        float minimum = At<float>(controller, 0x2A4);
        float range = !(perception >= minimum) ? minimum : perception;   // the original's branch-free select
        float radius = StoreFloat(static_cast<float>(static_cast<double>(range) * HearingScale() + HearingMargin()));
        SectorMapFindUnitsInRadius(world->sectorMap_, nullptr, &At<std::uint8_t>(unit, 0x1C), &heard, radius);
        for (auto* entry = heard.head; entry; entry = entry->next) *out++ = entry->data;
        heard.vtable = reinterpret_cast<void*>(kPtrListVtable);
        PtrListDestruct(&heard, nullptr);
    }
    return out;
}

// The timed objects (spells, effects) a player should know: those whose reach (slot 19 squared)
// covers its camera.
CEngineObject** CollectTimedObjects(CWorldServer* world, void* state, CEngineObject** out) {
    for (auto* node = world->timedObjects_.map.First(); node && node->value; node = world->timedObjects_.map.After(node)) {
        CEngineObject* object = node->value;
        auto* position = ei::VCall<const float*>(object, 20, &At<std::uint8_t>(state, kStateTimedContext));
        double dx = static_cast<double>(position[0]) - At<float>(state, kStateCameraX);
        double dy = static_cast<double>(position[1]) - At<float>(state, kStateCameraY);
        float distance = StoreFloat(static_cast<float>(dy * dy + dx * dx));
        float reach = StoreFloat(static_cast<float>(ei::VCall<double>(object, 19)));
        double reach2 = ei::VCall<double>(object, 19) * reach;
        if (reach2 > distance) *out++ = object;
    }
    return out;
}

} // namespace

int CWorldServer::SyncPlayer(WorldPlayer* player) {
    InitStatic(1, Collected(), kCollectedDestructor);
    InitStatic(2, Added(), kAddedDestructor);
    InitStatic(4, Kept(), kKeptDestructor);
    InitStatic(8, Removed(), kRemovedDestructor);

    void* state = At<void*>(player, kPlayerState);
    if (At<int>(state, kStateMode) == kModePlaying && unknown15C_ != 0) {
        std::uint32_t dirty = player->dirty;
        if (dirty & kDirtyZone) {
            int sent = SendZoneAt(this, nullptr, player);
            if (!sent) return sent;
            StateZoneSent(state, nullptr);
            StateClearUnits(state, nullptr);
            return StateFlush(state, nullptr);
        }
        if (dirty & kDirtyWeather) {
            int strength = x87::fistp(unknownE8_);
            // The original pushes EAX with the byte in AL (the rest is left from the dirty flags);
            // the callee reads the byte.
            if (!StateSendWeather(state, nullptr, *reinterpret_cast<std::uint8_t*>(&unknownE4_), strength)) return 0;
        }
        if (player->dirty & kDirtyTime) {
            float time;
            float speed;
            WorldTimeSettings(this, nullptr, &time, &speed);
            if (!StateSendTime(state, nullptr, time, speed)) return 0;
        }
        if (player->dirty & kDirtyWind) {
            Vector3 direction;
            std::memcpy(&direction, unknown24_, sizeof(direction));
            if (!StateSendWind(state, nullptr, direction, unknown30_)) return 0;
        }
        if (player->dirty & kDirtyCountdowns) StateSendCountdowns(state, nullptr, countdowns_);

        // Map objects: room for seven entries per world object.
        StaticPtrArray& collected = Collected();
        SetSize(collected, static_cast<int>(objects_.map.Size()) * 7);
        CEngineObject** end = CollectMapObjects(this, player, state, collected.data);
        SetSize(collected, static_cast<int>(end - Collected().data));
        DiffObjectSets(&At<std::uint8_t>(state, kStateSentObjects), &collected, &Added(), &Kept(), &Removed());
        StateObjectsKept(state, nullptr, &Kept());
        StateObjectsAdded(state, nullptr, &Added());
        StateObjectsRemoved(state, nullptr, &Removed());

        // Timed objects.
        SetSize(collected, static_cast<int>(timedObjects_.map.Size()));
        end = CollectTimedObjects(this, state, collected.data);
        SetSize(collected, static_cast<int>(end - Collected().data));
        DiffObjectSets(&At<std::uint8_t>(state, kStateSentTimed), &collected, &Added(), &Kept(), &Removed());
        StateTimedKept(state, nullptr, &Kept());
        StateTimedAdded(state, nullptr, &Added());
        StateTimedRemoved(state, nullptr, &Removed());

        if (unknown12D64_ == kKindSinglePlayer && IsMultiplayerClient()) {
            int& timer = At<int>(state, kStateRefreshTimer);
            if (--timer <= 0 && At<int>(state, kStateRefreshPending) == 0) StateRefresh(state, nullptr);
        }
    }

    int mode = At<int>(state, kStateMode);
    if ((mode == kModePlaying || mode == kModeJoining) && unknown15C_ != 0) StateUpdateUnits(state, nullptr);
    mode = At<int>(state, kStateMode);
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

    void* state = At<void*>(player, kPlayerState);
    int result;
    if (At<int>(state, kStateMode) != kModePlaying) {
        result = 1;
    } else {
        auto write = [&](const void* data, unsigned size) { ei::VCall<void>(&message, 2, data, size); };
        int previousTick = tick_ - 1;
        write(&previousTick, 4);
        write(mapName_, static_cast<unsigned>(std::strlen(mapName_) + 1));
        write(mobName_, static_cast<unsigned>(std::strlen(mobName_) + 1));
        int exits = exitCount_;
        write(&exits, 4);
        for (int i = 0; i < exitCount_; ++i) ZoneExitWrite(&exits_[i], nullptr, &message);
        write(&unknownE0_, 4);

        // The zone's units.
        alignas(4) std::uint8_t sets[0x28];
        void* units = sets;
        void* second = sets + 0x14;
        UnitSetConstruct(units, nullptr);
        SecondSetConstruct(second, nullptr);
        for (auto* node = mapObjects_.map.First(); node && node->value; node = mapObjects_.map.After(node))
            if (node->value->GetClassId() == world::kClassUnit) UnitSetAdd(units, nullptr, node->value);
        UnitSetWrite(units, nullptr, &message);
        SecondSetDestruct(second, nullptr);
        UnitSetDestruct(units, nullptr);

        result = StateSendZone(state, nullptr, &message);
        if (result != 0 && At<std::uint8_t>(this, 0x11C) != 0) {
            result = 1;
            if (At<int>(player, kPlayerUnitCount) != 0) {
                // The player starts at its first unit, on the ground there.
                auto* first = At<UnitMap>(player, kPlayerUnits).First();
                const float* at = &At<float>(first ? first->value : nullptr, 0x1C);
                Vector3 start;
                std::memcpy(&start, at, sizeof(start));
                start.z = static_cast<float>(ei::VCall<double>(this, 28, start.x, start.y));
                result = StateSendStart(state, nullptr, &start, 0, unknown12D64_);
            }
        }
        if (result != 0) ei::VCall<void>(state, 17, &At<std::uint8_t>(player, kPlayerObjects), 0);
    }
    ei::SetVtable(&message, kMemoryStreamVtable);
    if (message.data_) gamecrt::operator_delete(message.data_);
    return result;
}

EI_REPLACE_METHOD(0x0058B750, CWorldServer::SyncPlayer);
EI_REPLACE_METHOD(0x0058C890, CWorldServer::SendZone);
