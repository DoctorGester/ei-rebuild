// Checks for the world server's tick, unit waking, alarms and game result (src/game/world_tick.cpp).
#include "verify.h"

#include "../../src/game/unit.h"
#include "../../src/game/world.h"

#include <algorithm>
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
bool MissilesConsistent(const ProjectileList& missiles) {
    const auto& list = missiles.list;
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
                      self->projectiles_.list.count);
}
EI_VERIFY(0x0058DCD0, TickVerify, 0, s_unused);

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
