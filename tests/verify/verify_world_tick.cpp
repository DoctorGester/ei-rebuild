// Checks for the world server's tick, unit waking, alarms and game result (src/game/world_tick.cpp).
#include "verify.h"

#include "../../src/game/party.h"
#include "../../src/game/unit.h"
#include "../../src/game/world.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace {
void* s_unused;

using Entries = std::vector<std::pair<unsigned, CEngineObject*>>;

// The map's (key, value) pairs, sorted by key.
Entries Snapshot(const ObjectIdMap& map) {
    Entries entries;
    map.map.ForEach([&](const auto* node) { entries.emplace_back(node->key, node->value); });
    std::sort(entries.begin(), entries.end());
    return entries;
}

// Walks the missile list both ways: links agree, the last node is the tail, the count matches.
bool MissilesConsistent(const ProjectileList& list) {
    int count = 0;
    const ProjectileList::Node* previous = nullptr;
    for (const ProjectileList::Node* node = list.head; node; node = node->next) {
        if (node->prev != previous || ++count > list.count) return false;
        previous = node;
    }
    return previous == list.tail && count == list.count;
}
} // namespace

// 0058DCD0: one world tick. It runs every timed object, missile, command queue and unit, so only
// ours runs (stolenBytes 0): the tick advances by one and the missile list stays consistent.
static void __fastcall TickVerify(CWorldServer* self, void*, char force) {
    int tick = self->tick_;
    self->Tick(force);
    bool ok = self->tick_ == tick + 1 && MissilesConsistent(self->projectiles_);
    self->timedObjects_.map.ForEach([&](const auto* node) { ok = ok && node->value; });
    ei::verify::Check("CWorldServer::Tick", true, ok, "tick %d force %d missiles %d", self->tick_, force,
                      self->projectiles_.count);
}
EI_VERIFY(0x0058DCD0, TickVerify, 0, s_unused);

// 0058D670: runs every awake unit (or map object), so only ours runs (stolenBytes 0). Afterwards
// no id of the list of units sent to sleep (007AFEE0) is still in the update map.
static void __fastcall UpdateUnitsVerify(CWorldServer* self, void*) {
    self->UpdateUnits();
    struct Ids {
        const unsigned* data;
        int size;
    };
    const Ids& sleeping = *reinterpret_cast<const Ids*>(0x007AFEE0);
    const Ids& removed = *reinterpret_cast<const Ids*>(0x007AFEF0);
    char what[96] = "";
    for (int i = 0; i < sleeping.size && !what[0]; ++i)
        if (self->updateMap_.map.Find(sleeping.data[i])) std::snprintf(what, sizeof(what), "asleep %u still awake", sleeping.data[i]);
    ei::verify::Check("CWorldServer::UpdateUnits", true, what[0] == 0, "mode %d, %d asleep, %d removed: %s",
                      self->unknown12D64_, sleeping.size, removed.size, what);
}
EI_VERIFY(0x0058D670, UpdateUnitsVerify, 0, s_unused);

// 005935C0 (world_server.cpp): destructive (slots, releases), so only ours runs. Afterwards the id
// is in none of the id maps; an unknown id (only with FixRemoveMapObject on) changes nothing.
static void __fastcall RemoveObjectVerify(CWorldServer* self, void*, unsigned id) {
    bool known = self->objects_.map.Find(id) != nullptr;
    std::size_t before = self->objects_.map.Size();
    self->RemoveObject(id);
    char what[96] = "";
    if (!known && self->objects_.map.Size() != before)
        std::snprintf(what, sizeof(what), "unknown id, objects %u -> %u", static_cast<unsigned>(before),
                      static_cast<unsigned>(self->objects_.map.Size()));
    else if (self->objects_.map.Find(id))
        std::snprintf(what, sizeof(what), "still a world object");
    else if (self->updateMap_.map.Find(id))
        std::snprintf(what, sizeof(what), "still in the update map");
    else if (self->mapObjects_.map.Find(id))
        std::snprintf(what, sizeof(what), "still a map object");
    else if (self->namedObjects_.map.Find(id))
        std::snprintf(what, sizeof(what), "still a named object");
    ei::verify::Check("CWorldServer::RemoveObject", true, what[0] == 0, "id %u%s: %s", id, known ? "" : " (unknown)", what);
}
EI_VERIFY(0x005935C0, RemoveObjectVerify, 0, s_unused);

// A-W13. 005939E0 (world_server.cpp) removes every unit: ours only. Afterwards no unit (class 0x50)
// is a world object. No caller in the exe (Rebirth's binaries refer to it), no scenario.
static void __fastcall RemoveAllUnitsVerify(CWorldServer* self) {
    self->RemoveAllUnits();
    int units = 0;
    self->objects_.map.ForEach([&](const auto* node) { units += node->value && node->value->GetClassId() == 0x50; });
    ei::verify::Check("CWorldServer::RemoveAllUnits", 0, units, "%u objects left",
                      static_cast<unsigned>(self->objects_.map.Size()));
}
EI_VERIFY(0x005939E0, RemoveAllUnitsVerify, 0, s_unused);

// 0058D340 / 00593AF0 (world_quests.cpp) send a unit's message to the players whose party has it,
// 00590160 moves a player's held items into its party: ours only. Checked for the last: the list is
// empty afterwards. The alarm's remarks; a unit's death; every zone change (CollectParty).
static void __fastcall TellPartiesVerify(CWorldServer* self, void*, CEngineObject* unit, std::uint32_t value) {
    self->TellParties(unit, value);
    ei::verify::Check("CWorldServer::TellParties", true, unit != nullptr, "value %u", value);
}
EI_VERIFY(0x0058D340, TellPartiesVerify, 0, s_unused);

static void __fastcall TellPartiesGoneVerify(CWorldServer* self, void*, CEngineObject* unit) {
    self->TellPartiesGone(unit);
    ei::verify::Check("CWorldServer::TellPartiesGone", true, unit != nullptr, "unit %d", unit ? unit->id_ : -1);
}
EI_VERIFY(0x00593AF0, TellPartiesGoneVerify, 0, s_unused);

static void __fastcall StorePlayerItemsVerify(CWorldServer* self, void*, WorldPlayer* player, class CParty* party) {
    int before = player ? player->items.count : 0;
    self->StorePlayerItems(player, party);
    bool ok = !player || (player->items.head == nullptr && player->items.count == 0 && player->items.blocks == nullptr);
    ei::verify::Check("CWorldServer::StorePlayerItems", true, ok, "%d items", before);
}
EI_VERIFY(0x00590160, StorePlayerItemsVerify, 0, s_unused);

// 00591070 CWorldServer::ClearPlayerItems (A-W14): ours only (the items leave the party and are
// released); the party's item list is empty afterwards. Network games only (the multiplayer zone
// switch, a party sent again): no scenario reaches it.
static void __fastcall ClearPlayerItemsVerify(CWorldServer* self, void*, WorldPlayer* player) {
    int before = player->Party()->items_.size;
    self->ClearPlayerItems(player);
    ei::verify::Check("CWorldServer::ClearPlayerItems", 0, player->Party()->items_.size, "%d items", before);
}
EI_VERIFY(0x00591070, ClearPlayerItemsVerify, 0, s_unused);

// 0058D3D0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x20 (6 bytes). Only adds to the update map: the
// original runs first, its entries are recorded, the map is put back (new keys erased, old values
// and the highest id restored), ours runs, and the entries and highest id must match.
static void(__fastcall* s_wake)(CWorldServer*, void*, const float*, float);
static void __fastcall WakeVerify(CWorldServer* self, void*, const float* position, float radius) {
    ObjectIdMap& map = self->updateMap_;
    Entries before = Snapshot(map);
    int maxBefore = map.maxId;
    s_wake(self, nullptr, position, radius);
    Entries theirs = Snapshot(map);
    int maxTheirs = map.maxId;
    for (const auto& entry : theirs) {
        auto old = std::lower_bound(before.begin(), before.end(), std::make_pair(entry.first, static_cast<CEngineObject*>(nullptr)));
        auto* node = map.map.Find(entry.first);
        if (old != before.end() && old->first == entry.first)
            node->value = old->second;
        else
            map.map.Erase(node);
    }
    map.maxId = maxBefore;
    self->WakeUnitsAround(position, radius);
    Entries mine = Snapshot(map);
    ei::verify::Check("CWorldServer::WakeUnitsAround", true, theirs == mine && maxTheirs == map.maxId,
                      "(%g %g) r=%g: %u/%u entries, max %d/%d", position[0], position[1], radius,
                      static_cast<unsigned>(theirs.size()), static_cast<unsigned>(mine.size()), maxTheirs, map.maxId);
}
EI_VERIFY(0x0058D3D0, WakeVerify, 6, s_wake);

// 00593FC0: tells the AI brains (stateful callees), so only ours runs. The five alarms must be
// exactly as expected, and when the alarm was raised every unit without a player whose brain
// reacts must be in the update map.
static void __fastcall InvokeAlarmVerify(CWorldServer* self, void*, int index, const float* point, char force) {
    WorldAlarm expected[5];
    std::memcpy(expected, self->alarms_, sizeof(expected));
    bool raised = index >= 0 && index < 5 && (!expected[index].active || force);
    self->InvokeAlarm(index, point, force);
    if (raised) {
        if (index == 0 && force) std::memset(expected, 0, sizeof(expected));
        expected[index].active = 1;
        expected[index].time = self->tick_;
        std::memcpy(&expected[index].x, &point[0], sizeof(float));
        std::memcpy(&expected[index].y, &point[1], sizeof(float));
    }
    bool ok = std::memcmp(expected, self->alarms_, sizeof(expected)) == 0;
    if (raised) {
        auto& objects = self->objects_.map;
        for (auto* node = objects.First(); node && node->value; node = objects.After(node)) {
            CEngineObject* object = node->value;
            if (object->GetClassId() != world::kClassUnit) continue;
            auto* unit = static_cast<CUnit*>(object);
            if (unit->player || !reinterpret_cast<std::uint8_t*>(unit->controller)[0x980 + 0x37]) continue;
            auto* entry = self->updateMap_.map.Find(static_cast<unsigned>(object->id_));
            ok = ok && entry && entry->value == object;
        }
    }
    ei::verify::Check("CWorldServer::InvokeAlarm", true, ok, "index %d force %d raised %d", index, force, raised);
}
EI_VERIFY(0x00593FC0, InvokeAlarmVerify, 0, s_unused);

// 0058DC90: replaces an object and calls it when the game ends, so only ours runs; the result is
// recorded only the first time.
static void __fastcall SetGameResultVerify(CWorldServer* self, void*, int result) {
    int before = self->gameResult_;
    self->SetGameResult(result);
    int expected = before != 0 ? before : result;
    ei::verify::Check("CWorldServer::SetGameResult", expected, self->gameResult_, "result %d (was %d)", result, before);
}
EI_VERIFY(0x0058DC90, SetGameResultVerify, 0, s_unused);

// A-W13. ObjectMapIterator: First 0059AF20 / 0059AFF0 (MOV EAX,[ESP+4]; PUSH EBX, 5 bytes) and Next
// 0059AF80 / 0059B050 (MOV EAX,[ECX]; PUSH EDI; TEST EAX,EAX, 5 bytes) only read the map, so the
// original runs on a scratch iterator and the two are compared. Their callers in the exe are
// rewritten and walk the maps themselves, so these run only for code outside the rewrite.
using IterFirstFn = CEngineObject*(__fastcall*)(ObjectMapIterator*, void*, ObjectIdMap*);
using IterNextFn = CEngineObject*(__fastcall*)(ObjectMapIterator*);
static CEngineObject* CheckedIterFirst(IterFirstFn original, ObjectMapIterator* self, ObjectIdMap* objects) {
    ObjectMapIterator theirs;
    CEngineObject* expected = original(&theirs, nullptr, objects);
    CEngineObject* actual = self->First(objects);
    bool same = expected == actual && theirs.node == self->node && theirs.table == self->table && theirs.map == self->map;
    ei::verify::Check("ObjectMapIterator::First", true, same, "%p vs %p", expected, actual);
    return actual;
}
static CEngineObject* CheckedIterNext(IterNextFn original, ObjectMapIterator* self) {
    ObjectMapIterator theirs = *self;
    CEngineObject* expected = original(&theirs);
    CEngineObject* actual = self->Next();
    bool same = expected == actual && theirs.node == self->node && theirs.table == self->table && theirs.map == self->map;
    ei::verify::Check("ObjectMapIterator::Next", true, same, "%p vs %p", expected, actual);
    return actual;
}
static IterFirstFn s_iterFirstA, s_iterFirstB;
static IterNextFn s_iterNextA, s_iterNextB;
static CEngineObject* __fastcall IterFirstVerifyA(ObjectMapIterator* self, void*, ObjectIdMap* objects) {
    return CheckedIterFirst(s_iterFirstA, self, objects);
}
static CEngineObject* __fastcall IterFirstVerifyB(ObjectMapIterator* self, void*, ObjectIdMap* objects) {
    return CheckedIterFirst(s_iterFirstB, self, objects);
}
static CEngineObject* __fastcall IterNextVerifyA(ObjectMapIterator* self) { return CheckedIterNext(s_iterNextA, self); }
static CEngineObject* __fastcall IterNextVerifyB(ObjectMapIterator* self) { return CheckedIterNext(s_iterNextB, self); }
EI_VERIFY(0x0059AF20, IterFirstVerifyA, 5, s_iterFirstA);
EI_VERIFY(0x0059AFF0, IterFirstVerifyB, 5, s_iterFirstB);
EI_VERIFY(0x0059AF80, IterNextVerifyA, 5, s_iterNextA);
EI_VERIFY(0x0059B050, IterNextVerifyB, 5, s_iterNextB);

// A-W13. The server's height slots: 0058A390 (SUB ESP,0xC; MOV EAX,[ESP+0x10], 7 bytes), 0058A3D0 /
// 0058A3F0 (MOV EDX,[ESP+8]; MOV EAX,[ECX], 6 bytes). Pure; compared as floats. The units'
// placement and movement (`newgame`).
using HeightFn = double(__fastcall*)(CWorldServer*, void*, float, float);
static HeightFn s_serverRawHeight, s_serverGroundHeight, s_serverSurfaceHeight;
static double __fastcall ServerRawHeightVerify(CWorldServer* self, void*, float x, float y) {
    float expected = static_cast<float>(s_serverRawHeight(self, nullptr, x, y));
    double result = self->ServerRawSurfaceHeight(x, y);
    ei::verify::CheckNear("CWorldServer::ServerRawSurfaceHeight", expected, static_cast<float>(result), "(%g, %g)", x, y);
    return result;
}
EI_VERIFY(0x0058A390, ServerRawHeightVerify, 7, s_serverRawHeight);

static double __fastcall ServerGroundHeightVerify(CWorldServer* self, void*, float x, float y) {
    float expected = static_cast<float>(s_serverGroundHeight(self, nullptr, x, y));
    double result = self->ServerGroundHeight(x, y);
    ei::verify::CheckNear("CWorldServer::ServerGroundHeight", expected, static_cast<float>(result), "(%g, %g)", x, y);
    return result;
}
EI_VERIFY(0x0058A3D0, ServerGroundHeightVerify, 6, s_serverGroundHeight);

static double __fastcall ServerSurfaceHeightVerify(CWorldServer* self, void*, float x, float y) {
    float expected = static_cast<float>(s_serverSurfaceHeight(self, nullptr, x, y));
    double result = self->ServerSurfaceHeight(x, y);
    ei::verify::CheckNear("CWorldServer::ServerSurfaceHeight", expected, static_cast<float>(result), "(%g, %g)", x, y);
    return result;
}
EI_VERIFY(0x0058A3F0, ServerSurfaceHeightVerify, 6, s_serverSurfaceHeight);
