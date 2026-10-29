// A unit's own items (units outside the party keep them in CUnit; party units' items live in the
// player's records, unit.h): armour, the carried items array, releasing everything, and sending
// the equipment to the controller. See unit.h and docs/notes/units.md.
#include "unit.h"

#include "item.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include "unit_create.h"
#include "unit_db.h"

#include <bit>
#include <cstring>

namespace {
constexpr int kPartEquipment = 5;
constexpr int kPartStats = 4;



void ReleaseSlots(CUnit* unit, CItem** slots, int count) {
    for (int i = 0; i < count; ++i) {
        CItem* item = slots[i];
        slots[i] = nullptr;
        if (item) item->Release();
        unit->MarkChanged(kPartEquipment);
    }
}
} // namespace

// The unit prototype table's record named `name`.

CItemArmor* CUnit::EquipArmour(CItem* item) {
    if (!item) return nullptr;
    if (x87::fistp(item->condition) <= 0) return nullptr;    // broken
    CItemArmor* piece = AsArmor(item);
    CItemArmor* previous = armor[piece->GetType()];
    armor[piece->GetType()] = piece;
    piece->AddRef();
    Controller()->ArmourPutOn(piece->prototypeId);
    if (previous) previous->Release();
    MarkChanged(kPartEquipment);
    return previous;
}

CItemArmor* CUnit::UnequipArmour(int slot) {
    CItemArmor* item = armor[slot];
    armor[slot] = nullptr;
    if (item) {
        Controller()->ArmourTakenOff(static_cast<std::uint32_t>(item->GetType()));
        item->Release();
    }
    MarkChanged(kPartEquipment);
    MarkChanged(kPartStats);
    return item;
}

void CUnit::ReleaseAllItems() {
    ReleaseSlots(this, weapons, 4);
    for (CItemArmor*& slot : armor) {
        CItemArmor* item = slot;
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
    for (CItem* item : carried) item->Release();
    carried.clear();
}

void CUnit::AddCarried(CItem* item) {
    item->AddRef();
    carried.push_back(item);
    MarkChanged(kPartEquipment);
}

int CUnit::RemoveCarried(int index) {
    carried[index]->Release();
    carried.erase(carried.begin() + index);
    return static_cast<int>(std::ssize(carried));
}

void CUnit::SendEquipment() {
    std::uint32_t ids[11];   // 4 weapons, then 7 armour pieces (-1 for none)
    GetEquipmentIds(ids);
    Controller()->SetArmourModels(&ids[4]);
    Controller()->SetWeaponModels(&ids[0]);
}

std::uint32_t CUnit::SetSecondPrototypeByName(std::string_view prototypeName) {
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

int CUnit::SetSecondPrototypeIndex(int index) {
    secondPrototype = index;
    return index;
}
