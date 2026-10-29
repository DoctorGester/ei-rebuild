// WorldServer::Loot: a player takes what a dead body or a container holds. See
// world.h and docs/notes/world.md.
#include "unit.h"
#include "world.h"

#include "map_object.h"
#include "object.h"
#include "player_messages.h"
#include "party.h"

#include "effects.h"
#include <cstdint>

#include "game_singletons.h"

#include <list>

namespace {



} // namespace

bool WorldServer::Loot(CUnit* unit, CPlayer* player) {
    std::list<Item*> items;
    std::list<Item*> second;
    int gold;
    // The early returns empty the lists without their destructors' bookkeeping; the others destroy them.
    auto abandon = [&]() {
        second.clear();
        items.clear();
    };
    auto destroy = [&]() {
    };

    if (!player) {
        abandon();
        return false;
    }
    if (unit->HasPlayer() && unit->IsDead() == 0) {
        abandon();
        return false;   // a living unit of a player
    }

    if (unit->flags & MapObjectFlag::Shared) {
        // The shared-object list's entry for the object (SharedObject: its unknown_08 is the gold).
        std::list<SharedObject>& list = sharedObjects;
        auto node = std::ranges::find(list, unit->id, &SharedObject::objectId);
        if (node == list.end()) {
            abandon();
            return false;
        }
        // The entry's references go with the items (the original frees the node without releasing them).
        for (HeldRef<Item>& item : node->objects) items.push_back(item.Take());
        player->Party()->money += static_cast<int>(node->money);
        player->GiveItems(&items, 0);
        list.erase(node);
        abandon();
        return true;
    }

    if (!hoover->CollectLoot(this, unit, &items, &second, &gold)) {
        destroy();
        return false;
    }
    if (IsMultiplayerClient()) {
        // Quest items go to player 0.
        CPlayer* first = nullptr;
        {
            PlayerIterator it;
            for (CPlayer* p = it.First(PlayerList()); p; p = it.Next()) {
                if (p->playerId == 0) {
                    first = p;
                    break;
                }
            }
        }
        if (first) {
            std::list<Item*> quest;
            for (auto node = items.begin(); node != items.end();) {
                auto next = std::next(node);
                Item* item = *node;
                if (item->GetClassId() == ClassId::ItemQuest) {
                    quest.push_back(item);
                    items.erase(node);
                }
                node = next;
            }
            if (!quest.empty()) first->GiveItems(&quest, 0);
            for (Item* item : quest)
                item->Release();
        }
    }
    player->Party()->money += gold;
    player->GiveItems(&items, 0);
    for (Item* item : items)
        item->Release();
    bool found = !items.empty() || gold > 0;
    destroy();
    return found;
}

// ---- LootDeadUnits ----------------------------------------------------------------------------

void WorldServer::LootDeadUnits() {
    // The walk starts before the player is looked up; unlike most walks it does not stop at an empty entry.
    auto node = objects.map.begin();
    CPlayer* local = nullptr;
    {
        PlayerIterator it;
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
            if (player->playerId == 0) {
                local = player;
                break;
            }
    }
    if (!local) return;

    std::list<unsigned> dead;
    for (; node != objects.map.end(); ++node) {
        EngineObject* object = node->second;
        CUnit* unit = UnitOrNull(object);   // no null test here: the map holds no null
        if (unit && unit->IsDead()) {
            g_serverWorld.Loot(unit, local);
            dead.push_back(static_cast<unsigned>(object->id));
        }
    }
    for (unsigned objectId : dead) g_serverWorld.RemoveObject(objectId);
}
