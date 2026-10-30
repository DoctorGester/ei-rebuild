// A unit's own items (units outside the party keep them in Unit; party units' items live in the
// player's records, unit.h): armour, the carried items array, releasing everything, and sending
// the equipment to the controller. See unit.h and docs/notes/units.md.
#include "unit.h"

#include "item.h"

#include "lib/crt.h"
#include "lib/rounding.h"
#include "unit_create.h"
#include "unit_db.h"

#include <bit>
#include <cstring>

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
