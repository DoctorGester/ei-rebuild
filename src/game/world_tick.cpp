// CWorldServer: the per-tick update (timed objects, missiles, countdowns), waking units near a
// point, alarms and the game result. See world.h.
#include "script.h"
#include "combat.h"
#include "spell.h"
#include "world.h"
#include "at_exit_functions.h"

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

#include <cstring>
#include <vector>
#include "game_lists.h"
#include "game_data.h"

namespace {

// WakeUnitsAround's function-local static: a SpiralCache (guard bit 0 of g_wakeCacheGuard, destructor registered
// with atexit).
SpiralCache g_wakeCache = /*007AFF08*/ {};
std::uint8_t g_wakeCacheGuard = /*007AFF10*/ {};
SpiralCache& WakeCache() { return g_wakeCache; }
std::uint8_t& WakeCacheGuard() { return g_wakeCacheGuard; }

// CSectorMap, inlined here (bounds check and address only). The unit list of sector (x, y), or null outside the
// map.
mfc::CTypedPtrList<CUnit>* SectorUnits(CSectorMap* map, int x, int y) {
    if (x < 0 || y < 0 || x >= map->width || y >= map->height) return nullptr;
    return &map->sectors[map->width * y + x].units;
}

// Sector of a world coordinate: the AI map cell (half units, rounded through a float), / 32.
int SectorOf(float world) { return x87::fistp(static_cast<float>(2.0f * world - 0.5f)) / 32; }

// STLport list<unsigned> on the game's node pool, as Tick builds it on the stack: 12-byte nodes
// {next, prev, value}; the constructor allocates the head node.
class IdList {
public:
    struct Node {
        Node* next;
        Node* prev;
        unsigned value;
    };

    IdList() {
        head_ = stlp::allocate_as<Node>();
        head_->next = head_->prev = head_;
    }
    // Frees any nodes left, then the head.
    ~IdList() {
        Clear();
        stlp::deallocate(head_, sizeof(Node));
    }
    IdList(const IdList&) = delete;
    IdList& operator=(const IdList&) = delete;

    // insert(end(), value): create the node, then link it.
    void PushBack(unsigned value) {
        auto* node = stlp::allocate_as<Node>();
        node->value = value;
        node->next = head_;
        node->prev = head_->prev;
        head_->prev->next = node;
        head_->prev = node;
    }
    // clear(): frees the nodes in order and empties the ring.
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

void CWorldServer::Tick(char force) {
    ++tick_;
    if ((tick_ & 0x3F) == 3) UpdateWeather();

    // Timed objects: each one's Update runs it; those that return 0 are released now and taken out of the map after
    // the walk (the walk reads the node again after the release).
    auto& timed = timedObjects_.map;
    IdList finished;
    for (auto node = timed.begin(); node != timed.end(); ++node) {
        CEngineObject* object = node->second;
        if (object->RunTick() == 0) {
            finished.PushBack(static_cast<unsigned>(object->id_));
            object->Release();
        }
    }
    for (IdList::Node* id = finished.Begin(); id != finished.End(); id = id->next) {
        if (auto found = timed.find(id->value); found != timed.end()) timed.erase(found);
    }
    finished.Clear();

    AIEvents()->Advance(tick_);
    // Every countdown counted down; true while one still runs.
    if (countdowns_.Tick()) MarkPlayersDirty(0x20);

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
        mfc::impl::FreeNodeAs<gamelists::Refs>(&projectiles_, current);   // the missile released
    }

    if (scriptsActive_ != 0 || force) ScriptTick(MainScript());
    ScriptTick(SecondScript());
    UpdateUnits();
}

void CWorldServer::WakeUnitsAround(const Vector3* position, float radius) {
    if (!(WakeCacheGuard() & 1)) {
        WakeCache().radius = -1.0f;
        WakeCacheGuard() |= 1;
        WakeCache().length = 0;
        libcrt::AtExit(ei::short_functions::Ret);
    }
    int count;
    const std::int16_t* offsets = WakeCache().Get(radius * 0.03125f, &count);   // radius in sectors
    int sx = SectorOf(position->x);
    int sy = SectorOf(position->y);
    CSectorMap* sectors = Sectors();
    for (int i = 0; i < count; ++i, offsets += 2) {
        auto* units = SectorUnits(sectors, offsets[0] + sx, offsets[1] + sy);
        if (!units) continue;
        for (auto* unitNode = units->head; unitNode;) {
            CUnit* unit = unitNode->data;
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
    for (auto node = objects.begin(); node != objects.end() && node->second; ++node) {
        CMapObject* object = node->second;
        if (CUnit* unit = UnitOrNull(object)) {
            if (!unit->player) {
                AIBrain* brain = unit->ServerController()->Brain();
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

namespace {
// UpdateUnits' two function-local id lists (arrays of data, size, max, grow; the data block starts one dword
// after its allocation), with their guard bits in g_updateListGuards.
using IdArray = std::vector<unsigned>;
constinit std::vector<unsigned> g_removedIds = /*007AFEF0*/ {};    // flagged for removal
constinit std::vector<unsigned> g_sleepingIds = /*007AFEE0*/ {};   // leave the update map
IdArray& RemovedIds() { return g_removedIds; }
IdArray& SleepingIds() { return g_sleepingIds; }
// The exit functions: each frees its array's block (the fields stay).
void FreeRemovedIds() { lib::FreeStorage(g_removedIds); }
void FreeSleepingIds() { lib::FreeStorage(g_sleepingIds); }
std::uint8_t g_updateListGuards = /*007AFF00*/ {};
std::uint8_t& UpdateListGuards() { return g_updateListGuards; }
} // namespace

void CWorldServer::UpdateUnits() {
    if (!(UpdateListGuards() & 1)) {
        UpdateListGuards() |= 1;
        RemovedIds() = IdArray{};
        libcrt::AtExit(FreeRemovedIds);
    }
    if (!(UpdateListGuards() & 2)) {
        UpdateListGuards() |= 2;
        SleepingIds() = IdArray{};
        libcrt::AtExit(FreeSleepingIds);
    }
    RemovedIds().clear();
    SleepingIds().clear();

    // The walk stops at the end or at an entry without an object.
    if (sessionKind_ != 1) {
        // Every map object runs (no sleeping).
        auto& objects = mapObjects_.map;
        for (auto node = objects.begin(); node != objects.end() && node->second; ++node) {
            CEngineObject* object = node->second;
            object->RunTick();
            if (object->flags_ & 1) RemovedIds().push_back(static_cast<unsigned>(object->id_));
        }
    } else {
        if (ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellEffectsOverflow)) TickSleepingEffects();
        // The units in the update map; a unit whose countdown runs out goes to sleep (leaves the map) unless its
        // controller is busy, which restarts the countdown.
        auto& awake = updateMap_.map;
        for (auto node = awake.begin(); node != awake.end() && node->second; ++node) {
            CEngineObject* object = node->second;
            object->RunTick();
            if (object->flags_ & 1) RemovedIds().push_back(static_cast<unsigned>(object->id_));
            CUnit* unit = UnitOrNull(object);
            if (!unit) continue;
            if (--unit->updateCountdown > 0) continue;   // updates left before it may sleep
            const CUnitServer* controller = unit->ServerController();
            if (controller->moveMode2.flag4 == 0 && controller->brain.alwaysUpdated == 0 &&
                controller->brain.memory.seesEnemy == 0)
                SleepingIds().push_back(static_cast<unsigned>(object->id_));
            else
                unit->updateCountdown = UpdateCountdownFor(object);
        }
    }

    // The counted objects run too; those whose Update returns 0 leave the list (its RemoveAt also releases the
    // reference).
    for (auto* position = counted_.head; position;) {
        auto* current = position;
        position = position->next;
        if (current->data->RunTick() == 0) mfc::impl::RemoveAtAs<gamelists::EngineObjects>(&counted_, current);   // the object released
    }
    for (int i = 0; i < std::ssize(SleepingIds()); ++i) {
        if (auto found = updateMap_.map.find(SleepingIds()[i]); found != updateMap_.map.end()) updateMap_.map.erase(found);
    }
    for (int i = 0; i < std::ssize(RemovedIds()); ++i) RemoveObject(RemovedIds()[i]);
}

void CWorldServer::TickSleepingEffects() {
    // Rebirth (a context hook): every map object that is referenced, not flagged for removal, has effects and a
    // companion, and is asleep gets one tick taken off each effect; when one runs out, the object goes back into
    // the update map, whose update removes it.
    for (const auto& [id, object] : mapObjects_.map) {
        if ((object->flags_ & 1) || object->refCount_ == 0 || object->effects.size == 0) continue;
        if (object->updateCountdown > 0 || !object->companion) continue;
        bool expired = false;
        for (int i = 0; i < object->effects.size; ++i) {
            if (--object->effects[i].ticksLeft <= 0) expired = true;
        }
        if (expired) AddToUpdateMap(object);
    }
}
