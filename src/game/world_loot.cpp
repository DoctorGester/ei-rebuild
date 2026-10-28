// CWorldServer::Loot: a player takes what a dead body or a container holds. See
// world.h and docs/notes/world.md.
#include "unit.h"
#include "world.h"

#include "map_object.h"
#include "object.h"
#include "player_messages.h"
#include "party.h"

#include "lib/crt.h"
#include "effects.h"
#include "lib/mfc.h"

#include "lib/mfc_list.h"
#include "game_lists.h"
#include "game_singletons.h"

#include <list>

namespace {

constexpr std::uint32_t kFlagSharedLoot = 0x20000000;     // flags_: the loot is on the shared list
constexpr int kClassQuestItem = 0x14;                     // given to player 0 in multiplayer

using ObjectList = std::list<CEngineObject*>;
using GivenItems = std::list<CItem*>;

} // namespace

bool CWorldServer::Loot(CUnit* unit, CPlayer* player) {
    GivenItems items;
    GivenItems second;
    ObjectList third;
    int gold;
    // The early returns empty the lists without their destructors' bookkeeping; the others destroy them.
    auto abandon = [&]() {
        third.clear();
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

    if (unit->flags_ & kFlagSharedLoot) {
        // The shared-object list's entry for the object (SharedObject: its unknown_08 is the gold).
        SharedObjectList& list = sharedObjects_;
        auto node = std::ranges::find(list, unit->id_, &SharedObject::objectId);
        if (node == list.end()) {
            abandon();
            return false;
        }
        // The entry's references go with the items (the original frees the node without releasing them).
        for (HeldPtr<CItem>& item : node->objects) items.push_back(item.release());
        player->Party()->Money() += static_cast<int>(node->money);
        player->GiveItems(&items, 0);
        list.erase(node);
        abandon();
        return true;
    }

    if (!hoover_->CollectLoot(this, unit, &items, &second, &third, &gold)) {
        destroy();
        return false;
    }
    if (IsMultiplayerClient()) {
        // Quest items go to player 0.
        CPlayer* first = nullptr;
        {
            PlayerIterator it;
            for (CPlayer* p = it.First(PlayerList()); p; p = it.Next()) {
                if (p->playerId_ == 0) {
                    first = p;
                    break;
                }
            }
        }
        if (first) {
            GivenItems quest;
            for (auto node = items.begin(); node != items.end();) {
                auto next = std::next(node);
                CItem* item = *node;
                if (item->GetClassId() == kClassQuestItem) {
                    quest.push_back(item);
                    items.erase(node);
                }
                node = next;
            }
            if (!quest.empty()) first->GiveItems(&quest, 0);
            for (CItem* item : quest)
                item->Release();
        }
    }
    player->Party()->Money() += gold;
    player->GiveItems(&items, 0);
    for (CItem* item : items)
        item->Release();
    bool found = !items.empty() || gold > 0;
    destroy();
    return found;
}

// ---- LootDeadUnits ----------------------------------------------------------------------------

namespace {

} // namespace

void CWorldServer::LootDeadUnits() {
    // The walk starts before the player is looked up; unlike most walks it does not stop at an empty entry.
    auto node = objects_.map.begin();
    CPlayer* local = nullptr;
    {
        PlayerIterator it;
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
            if (player->playerId_ == 0) {
                local = player;
                break;
            }
    }
    if (!local) return;

    std::list<unsigned> dead;
    for (; node != objects_.map.end(); ++node) {
        CEngineObject* object = node->second;
        CUnit* unit = UnitOrNull(object);   // no null test here: the map holds no null
        if (unit && unit->IsDead()) {
            TheServerWorld()->Loot(unit, local);
            dead.push_back(static_cast<unsigned>(object->id_));
        }
    }
    for (unsigned id : dead) TheServerWorld()->RemoveObject(id);
}
