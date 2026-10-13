// CWorldServer: the per-tick update (timed objects, missiles, countdowns), waking units near a
// point, alarms and the game result. See world.h.
#include "world.h"

#include "core_maths.h"
#include "map_object.h"
#include "sector_map.h"
#include "unit.h"
#include "unit_ai.h"

#include "lib/containers.h"
#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
#include "ei/x87.h"

#include <cstring>
#include "originals_library.h"
#include "originals_world.h"

namespace {

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ------------

// 0058CCD0. Every 64 ticks: weather (rain, lightning strikes), day time.
__declspec(naked) void __fastcall ServerUpdateWeather(CWorldServer*, void*) EI_ORIGINAL(0x0058CCD0)
// 0058D670. Runs the units in the update map (rewritten below; called through its address so a
// Rebirth launch without the flag still runs addon's patched original).
__declspec(naked) void __fastcall ServerUpdateUnits(CWorldServer*, void*) EI_ORIGINAL(0x0058D670)
// 0059A050. RemoveAt of the counted-reference list (+0x12E44): unlinks the node, releases its
// object (slot 4) and frees the node.
__declspec(naked) void __fastcall CountedRemoveAt(mfc::CTypedPtrList<CEngineObject>*, void*,
                                                  mfc::CTypedPtrList<CEngineObject>::Node*) EI_ORIGINAL(0x0059A050)
// 005935C0. Removes object `id` from the server (Rebirth's FixRemoveMapObject patches it).
// 0055F410. Replaces the object at +0x60 with a new one (vtable 0073F3A8).
__declspec(naked) void __fastcall ServerReplace60(CWorldServer*, void*) EI_ORIGINAL(0x0055F410)
// 005C9530. The time wheel (+0x2838): moves to `tick`, emptying that tick's 16 slots.
__declspec(naked) void __fastcall TimeWheelAdvance(void*, void*, int /*tick*/) EI_ORIGINAL(0x005C9530)
// 006D44E0. Counts every countdown (+0x70) down; true (AL) when one was still running.
__declspec(naked) bool __fastcall CountdownsTick(void*, void*) EI_ORIGINAL(0x006D44E0)
// 004BF000. Runs a script command queue (+0x12874 / +0x12A6C).
__declspec(naked) void __fastcall CommandQueueRun(void*, void*) EI_ORIGINAL(0x004BF000)
// 005D5940. An AI brain (unit controller + 0x980) hears alarm `index`.
void __fastcall BrainHearAlarm(void* brain, void*, int index) { static_cast<AIBrain*>(brain)->HearAlarm(index); }
// The game's atexit (006E727B).

constexpr int kSlotRelease60 = 4;     // the +0x60 object's slot 4, called after it is replaced
constexpr int kBrainOffset = 0x980;   // AI brain inside the unit controller
constexpr int kBrainAlarmed = 0x37;   // brain byte: reacting to an alarm

// WakeUnitsAround's function-local static: a SpiralCache at 007AFF08 (guard bit 007AFF10 & 1,
// destructor 0058D5C0 registered with atexit).
SpiralCache& WakeCache() { return *reinterpret_cast<SpiralCache*>(0x007AFF08); }
std::uint8_t& WakeCacheGuard() { return *reinterpret_cast<std::uint8_t*>(0x007AFF10); }
constexpr std::uintptr_t kWakeCacheDestructor = 0x0058D5C0;

// 005C0930 (CSectorMap, inlined here: bounds check and address only). The unit list of sector
// (x, y), or null outside the map.
mfc::CTypedPtrList<CEngineObject>* SectorUnits(CSectorMap* map, int x, int y) {
    if (x < 0 || y < 0 || x >= map->width || y >= map->height) return nullptr;
    return &map->sectors[map->width * y + x].units;
}

// Sector of a world coordinate: the AI map cell (half units, rounded through a float), / 32.
int SectorOf(float world) { return x87::fistp(StoreFloat(2.0f * world - 0.5f)) / 32; }

// STLport list<unsigned> on the game's node pool, as Tick builds it on the stack: 12-byte nodes
// {next, prev, value}; the constructor (00404A70) allocates the head node.
class IdList {
public:
    struct Node {
        Node* next;
        Node* prev;
        unsigned value;
    };

    IdList() {
        head_ = static_cast<Node*>(stlp::allocate(sizeof(Node)));
        head_->next = head_->prev = head_;
    }
    // 0056F150: frees any nodes left, then the head.
    ~IdList() {
        Clear();
        stlp::deallocate(head_, sizeof(Node));
    }
    IdList(const IdList&) = delete;
    IdList& operator=(const IdList&) = delete;

    // insert(end(), value): 00404C90 creates the node, 00404A40 links it.
    void PushBack(unsigned value) {
        auto* node = static_cast<Node*>(stlp::allocate(sizeof(Node)));
        node->value = value;
        node->next = head_;
        node->prev = head_->prev;
        head_->prev->next = node;
        head_->prev = node;
    }
    // clear(): frees the nodes in order (00404D10) and empties the ring.
    void Clear() {
        for (Node* node = head_->next; node != head_;) {
            Node* next = node->next;
            stlp::deallocate(node, sizeof(Node));
            node = next;
        }
        head_->next = head_->prev = head_;
    }

    Node* Begin() const { return head_->next; }
    Node* End() const { return head_; }

private:
    Node* head_;
};

} // namespace

__declspec(naked) int Projectile::Update() EI_ORIGINAL(0x0067E6D0)
// 0059C810. Releases the node's missile and returns the node to the free list (the last one frees
// every block).
__declspec(naked) void __fastcall ProjectileFreeNode(ProjectileList*, void*, ProjectileList::Node*) EI_ORIGINAL(0x0059C810)

// ---- The tick ------------------------------------------------------------------------------------

void CWorldServer::Tick(char force) {
    ++tick_;
    if ((tick_ & 0x3F) == 3) ServerUpdateWeather(this, nullptr);

    // Timed objects: each one's slot 6 runs it; those that return 0 are released now and taken
    // out of the map after the walk (the walk reads the node again after the release).
    auto& timed = timedObjects_.map;
    auto* node = timed.First();
    CEngineObject* object = node ? node->value : nullptr;
    IdList finished;
    while (node) {
        if (object->RunTick() == 0) {
            finished.PushBack(static_cast<unsigned>(object->id_));
            object->Release();
        }
        node = timed.After(node);
        object = node ? node->value : nullptr;
    }
    for (IdList::Node* id = finished.Begin(); id != finished.End(); id = id->next) {
        if (auto* found = timed.Find(id->value)) timed.Erase(found);
    }
    finished.Clear();

    TimeWheelAdvance(timeWheel_, nullptr, tick_);
    if (CountdownsTick(countdowns_, nullptr)) MarkPlayersDirty(0x20);

    // Missiles: those whose Update returns 0 are unlinked and freed.
    auto& missiles = projectiles_;
    for (ProjectileList::Node* next = missiles.head; next;) {
        ProjectileList::Node* current = next;
        next = current->next;
        if (current->data->Update() != 0) continue;
        if (current == missiles.head)
            missiles.head = current->next;
        else
            current->prev->next = current->next;
        if (current == missiles.tail)
            missiles.tail = current->prev;
        else
            current->next->prev = current->prev;
        ProjectileFreeNode(&projectiles_, nullptr, current);
    }

    if (unknown160_ != 0 || force) CommandQueueRun(commandsA_, nullptr);
    CommandQueueRun(commandsB_, nullptr);
    ServerUpdateUnits(this, nullptr);
}

void CWorldServer::WakeUnitsAround(const float* position, float radius) {
    if (!(WakeCacheGuard() & 1)) {
        WakeCache().radius = -1.0f;
        WakeCacheGuard() |= 1;
        WakeCache().length = 0;
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(kWakeCacheDestructor));
    }
    int count;
    const std::int16_t* offsets = WakeCache().Get(radius * 0.03125f, &count);   // radius in sectors
    int sx = SectorOf(position[0]);
    int sy = SectorOf(position[1]);
    auto* sectors = reinterpret_cast<CSectorMap*>(sectorMap_);
    for (int i = 0; i < count; ++i, offsets += 2) {
        auto* units = SectorUnits(sectors, offsets[0] + sx, offsets[1] + sy);
        if (!units) continue;
        for (auto* unitNode = units->head; unitNode;) {
            CEngineObject* unit = unitNode->data;
            unitNode = unitNode->next;
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
    auto* node = objects.First();
    if (!node) return;
    for (CEngineObject* object = node->value; object; object = node->value) {
        if (object->GetClassId() == world::kClassUnit) {
            auto* unit = static_cast<CUnit*>(object);
            if (!unit->player) {
                auto* brain = reinterpret_cast<std::uint8_t*>(unit->controller) + kBrainOffset;
                BrainHearAlarm(brain, nullptr, index);
                if (brain[kBrainAlarmed]) AddToUpdateMap(object);
            }
        }
        node = objects.After(node);
        if (!node) return;
    }
}

void CWorldServer::SetGameResult(int result) {
    if (gameResult_ != 0) return;
    gameResult_ = result;
    if (result == 2 || result == 3) {
        ServerReplace60(this, nullptr);
        ei::VCall<void>(unknown60_, kSlotRelease60);
    }
}

// ---- The units (0058D670) -----------------------------------------------------------------------

namespace {
// UpdateUnits' two function-local id lists (00453A50-style arrays: data, size, max, grow; the data
// block starts one dword after its allocation), with their guard bits at 007AFF00.
using IdArray = lib::AlignedArray<unsigned>;
// 004538D0: the instantiation's SetAtGrow(index, value) (RET 8).
__declspec(naked) void __fastcall IdArraySetAtGrow(IdArray*, void*, int, unsigned) EI_ORIGINAL(0x004538D0)

IdArray& RemovedIds() { return *reinterpret_cast<IdArray*>(0x007AFEF0); }   // flagged for removal
IdArray& SleepingIds() { return *reinterpret_cast<IdArray*>(0x007AFEE0); }  // leave the update map
std::uint8_t& UpdateListGuards() { return *reinterpret_cast<std::uint8_t*>(0x007AFF00); }
constexpr std::uintptr_t kRemovedIdsDestructor = 0x0058DA20;
constexpr std::uintptr_t kSleepingIdsDestructor = 0x0058DA00;

constexpr int kUnitSleepCountdown = 0x174;   // CUnit +0x174: updates left before it may sleep
int& SleepCountdown(CEngineObject* object) {
    return *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(object) + kUnitSleepCountdown);
}
// What AddToUpdateMap also writes: 0x80..0x8F updates, spread by the object's address.
int WakeCountdown(const CEngineObject* object) { return ((reinterpret_cast<int>(object) >> 5) & 0xF) + 0x80; }
} // namespace

void CWorldServer::UpdateUnits() {
    if (!(UpdateListGuards() & 1)) {
        UpdateListGuards() |= 1;
        RemovedIds() = IdArray{};
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(kRemovedIdsDestructor));
    }
    if (!(UpdateListGuards() & 2)) {
        UpdateListGuards() |= 2;
        SleepingIds() = IdArray{};
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(kSleepingIdsDestructor));
    }
    RemovedIds().RemoveAll();
    SleepingIds().RemoveAll();

    // The walk stops at the end or at an entry without an object.
    if (unknown12D64_ != 1) {
        // Every map object runs (no sleeping).
        auto& objects = mapObjects_.map;
        for (auto* node = objects.First(); node && node->value; node = objects.After(node)) {
            CEngineObject* object = node->value;
            object->RunTick();
            if (object->flags_ & 1) IdArraySetAtGrow(&RemovedIds(), nullptr, RemovedIds().size, static_cast<unsigned>(object->id_));
        }
    } else {
        if (ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellEffectsOverflow)) TickSleepingEffects();
        // The units in the update map; a unit whose countdown runs out goes to sleep (leaves the
        // map) unless its controller is busy (+0x174, +0x9B7, +0xB0C), which restarts the countdown.
        auto& awake = updateMap_.map;
        for (auto* node = awake.First(); node && node->value; node = awake.After(node)) {
            CEngineObject* object = node->value;
            object->RunTick();
            if (object->flags_ & 1) IdArraySetAtGrow(&RemovedIds(), nullptr, RemovedIds().size, static_cast<unsigned>(object->id_));
            if (object->GetClassId() != world::kClassUnit) continue;
            if (--SleepCountdown(object) > 0) continue;
            auto* controller = reinterpret_cast<const std::uint8_t*>(static_cast<CUnit*>(object)->controller);
            if (controller[0x174] == 0 && controller[0x9B7] == 0 && controller[0xB0C] == 0)
                IdArraySetAtGrow(&SleepingIds(), nullptr, SleepingIds().size, static_cast<unsigned>(object->id_));
            else
                SleepCountdown(object) = WakeCountdown(object);
        }
    }

    // The counted objects run too; those whose slot 6 returns 0 leave the list (its RemoveAt,
    // 0059A050, also releases the reference).
    for (auto* position = counted_.head; position;) {
        auto* current = position;
        position = position->next;
        if (current->data->RunTick() == 0) CountedRemoveAt(&counted_, nullptr, current);
    }
    for (int i = 0; i < SleepingIds().size; ++i) {
        if (auto* found = updateMap_.map.Find(SleepingIds().data[i])) updateMap_.map.Erase(found);
    }
    for (int i = 0; i < RemovedIds().size; ++i) WorldRemoveObject(this, nullptr, RemovedIds().data[i]);
}

void CWorldServer::TickSleepingEffects() {
    // Rebirth (addon.dll 1001813C, a context hook at 0058D82E): every map object that is referenced,
    // not flagged for removal, has effects and a companion, and is asleep (+0x174 <= 0; its own
    // update would count its effects down) gets one tick taken off each effect; when one runs
    // out, the object goes back into the update map, whose update removes it.
    mapObjects_.map.ForEach([this](auto* node) {
        auto* object = static_cast<CMapObject*>(node->value);
        if ((object->flags_ & 1) || object->refCount_ == 0 || object->effectCount == 0) return;
        if (object->unknown_174 > 0 || !object->companion) return;
        bool expired = false;
        for (int i = 0; i < object->effectCount; ++i) {
            if (--object->effects[i].ticksLeft <= 0) expired = true;
        }
        if (expired) AddToUpdateMap(object);
    });
}

EI_REPLACE_METHOD(0x0058DCD0, CWorldServer::Tick);
EI_REPLACE_METHOD(0x0058D3D0, CWorldServer::WakeUnitsAround);
EI_REPLACE_METHOD(0x00593FC0, CWorldServer::InvokeAlarm);
EI_REPLACE_METHOD(0x0058DC90, CWorldServer::SetGameResult);
EI_REPLACE_METHOD(0x0058D670, CWorldServer::UpdateUnits);
EI_REBIRTH_IMPLEMENTS(0x0058D670, ei::rebirth::Patch::FixSpellEffectsOverflow);
