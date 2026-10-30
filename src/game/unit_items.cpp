// A unit's own items (units outside the party keep them in Unit; party units' items live in the
// player's records, unit.h): armour, the carried items array, releasing everything, and sending
// the equipment to the controller. See unit.h and docs/notes/units.md.
#include "unit.h"

#include "item.h"
#include "random.h"
#include "unit_create.h"
#include "unit_db.h"
#include "lib/crt.h"
#include "lib/rounding.h"

#include <bit>
#include <cstring>
#include <vector>

namespace {
constexpr int kPartEquipment = 5;
constexpr int kPartStats = 4;

void ReleaseSlots(Unit* unit, Item** slots, int count) {
    for (int i = 0; i < count; ++i) {
        Item* item = slots[i];
        slots[i] = nullptr;
        if (item) item->Release();
        unit->MarkChanged(kPartEquipment);
    }
}
} // namespace

// The unit prototype table's record named `name`.

ItemArmor* Unit::EquipArmour(Item* item) {
    if (!item) return nullptr;
    if (lib::RoundEven(item->condition) <= 0) return nullptr;    // broken
    ItemArmor* piece = AsArmor(item);
    ItemArmor* previous = armor[piece->GetType()];
    armor[piece->GetType()] = piece;
    piece->AddRef();
    Controller()->ArmourPutOn(piece->prototypeId);
    if (previous) previous->Release();
    MarkChanged(kPartEquipment);
    return previous;
}

ItemArmor* Unit::UnequipArmour(int slot) {
    ItemArmor* item = armor[slot];
    armor[slot] = nullptr;
    if (item) {
        Controller()->ArmourTakenOff(static_cast<std::uint32_t>(item->GetType()));
        item->Release();
    }
    MarkChanged(kPartEquipment);
    MarkChanged(kPartStats);
    return item;
}

void Unit::ReleaseAllItems() {
    ReleaseSlots(this, weapons, 4);
    for (ItemArmor*& slot : armor) {
        ItemArmor* item = slot;
        slot = nullptr;
        if (item) {
            Controller()->ArmourTakenOff(static_cast<std::uint32_t>(item->GetType()));
            item->Release();
        }
        MarkChanged(kPartEquipment);
        MarkChanged(kPartStats);
    }
    Controller()->ClearOuterArmourLayers();
    ReleaseSlots(this, quickItems, 4);
    ReleaseSlots(this, spells, 8);
    for (Item* item : carried) item->Release();
    carried.clear();
}

void Unit::AddCarried(Item* item) {
    item->AddRef();
    carried.push_back(item);
    MarkChanged(kPartEquipment);
}

int Unit::RemoveCarried(int index) {
    carried[index]->Release();
    carried.erase(carried.begin() + index);
    return static_cast<int>(std::ssize(carried));
}

void Unit::SendEquipment() {
    auto ids = GetEquipmentIds();   // 4 weapons, then 7 armour pieces (-1 for none)
    Controller()->SetArmourModels(&ids[4]);
    Controller()->SetWeaponModels(&ids[0]);
}

std::uint32_t Unit::SetSecondPrototypeByName(std::string_view prototypeName) {
    UnitPrototypeRecord* record = g_unitDatabase.prototypes.FindByName(prototypeName);
    if (!record) {
        secondPrototype = -1;
        return 0xFFFFFFFFu;
    }
    int offset = unitdb::PrototypeByteOffset(record);
    secondPrototype = offset / static_cast<int>(sizeof(UnitPrototypeRecord));
    // The original returns the low half of the multiply by the reciprocal of 0x120, the original record size
    // (no caller reads it).
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(offset) * 0x38E38E39);
}

int Unit::SetSecondPrototypeIndex(int index) {
    secondPrototype = index;
    return index;
}

// --- Unit item lists ---------------------------------------------------------------------------------

// Unit::CollectItems: every item a unit holds and their spells as two lists, for the server controller's
// armour spell refresh. See unit.h.

void ItemsAndSpells::Add(Item* item) {
    if (!item) return;
    items.push_back(item);
    if (Spell* spell = item->spell) spells.push_back(spell);
}

namespace {
template <typename Item>
void AddSlots(ItemsAndSpells& held, Item* const* slots, int count) {
    for (int i = 0; i < count; ++i) held.Add(slots[i]);
}
} // namespace

ItemsAndSpells Unit::CollectItems() {
    ItemsAndSpells held;
    AddSlots(held, weapons, 4);
    AddSlots(held, armor, 7);
    AddSlots(held, quickItems, 4);
    AddSlots(held, spells, 8);
    for (Item* item : carried) held.Add(item);
    return held;
}

// --- Unit steal --------------------------------------------------------------------------------------

// Stealing an item from a unit. See unit.h (Unit::TakeRandomItem) and
// docs/notes/units.md ("Creation, databases, save").

namespace {
constexpr int kQuickSlots = 4;
constexpr int kPartItems = 5;     // MarkChanged part

} // namespace

Item* Unit::TakeRandomItem() {
    if (!carried.empty()) {
        // The first carried item, the rest moved down.
        Item* item = carried.front();
        carried.erase(carried.begin());
        MarkChanged(kPartItems);
        return item;
    }
    // The game collects the filled slots in an int array (grown one by one) and frees it afterwards; a
    // fixed array does the same.
    int filled[kQuickSlots];
    unsigned count = 0;
    for (int i = 0; i < kQuickSlots; ++i)
        if (quickItems[i]) filled[count++] = i;
    if (count == 0) return nullptr;
    int slot = filled[g_random.Next() % count];   // the draw is inlined
    Item* item = quickItems[slot];
    quickItems[slot] = nullptr;
    MarkChanged(kPartItems);
    return item;
}
