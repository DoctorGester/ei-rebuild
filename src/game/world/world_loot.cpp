// WorldServer::Loot: a player takes what a dead body or a container holds; Hoover's loot of a dead unit and its
// members. See world.h, hoover.h and docs/notes/world.md.
#include "game/world/world.h"

#include "game/app/singletons.h"
#include "engine/core/random.h"
#include "engine/data/text_manager.h"
#include "game/effects/effects.h"
#include "engine/models/figure_mesh.h"
#include "game/items/item.h"
#include "game/items/item_manager.h"
#include "game/objects/map_object.h"
#include "game/players/party.h"
#include "game/players/player_manager.h"
#include "game/players/player.h"
#include "game/units/unit.h"
#include "game/units/unit_db.h"
#include "game/world/hoover.h"

#include "lib/crt.h"
#include "lib/rounding.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

bool WorldServer::Loot(Unit* unit, Player* player) {
    std::vector<Item*> items;
    std::vector<Item*> second;
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
        std::vector<SharedObject>& list = sharedObjects;
        auto node = std::ranges::find(list, unit->id, &SharedObject::objectId);
        if (node == list.end()) {
            abandon();
            return false;
        }
        // The entry's references go with the items (the original frees the node without releasing them).
        for (HeldRef<Item>& item : node->objects) items.push_back(item.Take());
        player->GetParty()->money += static_cast<int>(node->money);
        player->GiveItems(items, 0);
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
        if (Player* first = GetPlayer(0)) {
            std::vector<Item*> quest;
            std::erase_if(items, [&quest](Item* item) {
                if (item->GetClassId() != ClassId::ItemQuest) return false;
                quest.push_back(item);
                return true;
            });
            if (!quest.empty()) first->GiveItems(quest, 0);
            for (Item* item : quest)
                item->Release();
        }
    }
    player->GetParty()->money += gold;
    player->GiveItems(items, 0);
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
    Player* local = GetPlayer(0);
    if (!local) return;

    std::vector<unsigned> dead;
    for (; node != objects.map.end(); ++node) {
        EngineObject* object = node->second;
        Unit* unit = UnitOrNull(object);   // no null test here: the map holds no null
        if (unit && unit->IsDead()) {
            Loot(unit, local);
            dead.push_back(static_cast<unsigned>(object->id));
        }
    }
    for (unsigned objectId : dead) RemoveObject(objectId);
}

// --- Hoover::CollectLoot -------------------------------------------------------------------------

namespace {

const char* const kHooverName = "CHoover";

// `object` appended with a new reference.
void Append(std::vector<Item*>* list, Item* object) {
    object->AddRef();
    list->push_back(object);
}

// One gold roll of the prototype: the amount (the minimum rounded, or a random value between the minimum and
// a larger maximum, truncated), kept when the percent roll (CRT rand) is within the chance.
void RollGold(const UnitGoldRoll& roll, int* gold) {
    float low = roll.minimum;
    float high = roll.maximum;
    int amount = lib::RoundEven(low);
    if (high > low) amount = static_cast<int>(g_random.Range(low, high));
    int percent = libcrt::RandBelow(100) + 1;
    if (static_cast<float>(percent) <= roll.chance) *gold += amount;
}

} // namespace

// The loot of a dead unit (the server's world calls it on its Hoover, +0x140, which the function does not
// use): the prototype's items (new objects from the item manager, put on both lists), the gold (two rolls of
// the prototype: an amount between a minimum and a maximum, kept when a percent roll is within the chance),
// the quick items and carried items, and for units with a text key also the armour and weapons, all taken off
// the unit and put on the drop list. A unit is looted once (flag 0x100000). Plain float maths for the amounts
// (rule 7).
int Hoover::CollectLoot(World* world, Unit* cunit, std::vector<Item*>* drops, std::vector<Item*>* items, int* gold) {
    bool keyed = IsTextKey(cunit->id);
    if (cunit->flags & MapObjectFlag::LootTaken) return 0;
    ItemManager* manager = world->ItemFactory();
    const UnitPrototypeRecord* prototype = cunit->GetPrototype();
    *gold = 0;
    if (prototype) {
        const std::vector<std::string>& itemNames = prototype->lootItems;
        for (std::size_t i = 0; i < itemNames.size(); ++i) {   // the end is read again each time
            Item* item = manager->CreateItem(itemNames[i]);
            if (!item) continue;
            Append(drops, item);
            items->push_back(item);   // without a new reference
        }
        const UnitPrototypeRecord* record = prototype;
        RollGold(record->gold, gold);
        RollGold(record->secondGold, gold);
        if (*gold < 0) *gold = 0;
    }
    for (int slot = 0; slot < 4; ++slot) {
        if (Item* item = cunit->GetQuickItem(slot)) Append(drops, item);
        cunit->RemoveQuickItem(slot);
    }
    for (Item* item : cunit->carried) {
        if (item) Append(drops, item);
    }
    while (!cunit->carried.empty()) cunit->RemoveCarried(0);
    if (keyed) {
        for (int slot = 0; slot < 7; ++slot) {
            ItemArmor* armour = cunit->WornItem(slot);
            if (!armour) continue;
            Append(drops, armour);
            cunit->UnequipArmour(static_cast<int>(armour->GetType()));
        }
        for (int slot = 0; slot < 4; ++slot) {
            ItemWeapon* weapon = cunit->GetWeapon(slot);
            if (!weapon) continue;
            Append(drops, weapon);
            cunit->RemoveWeapon(slot);
        }
    }
    cunit->flags |= MapObjectFlag::LootTaken;
    return 1;
}

// --- Object ------------------------------------------------------------------------------------------

Hoover::~Hoover() {}

void Hoover::Create() {}
const char* Hoover::ClassName() { return kHooverName; }
ClassId Hoover::GetClassId() { return ClassId::Hoover; }
