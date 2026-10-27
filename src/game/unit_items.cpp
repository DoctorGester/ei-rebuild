// A unit's own items (units outside the party keep them in CUnit; party units' items live in the
// player's records, unit.h): armour, the carried items array, releasing everything, and sending
// the equipment to the controller. See unit.h and docs/notes/units.md.
#include "unit.h"

#include "item.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "unit_create.h"
#include "unit_db.h"

#include <bit>
#include <cstring>

namespace {
constexpr int kPartEquipment = 5;
constexpr int kPartStats = 4;

constexpr int kCarriedGrowMin = 4;
constexpr int kCarriedGrowMax = 0x400;

void ReleaseSlots(CUnit* unit, CItem** slots, int count) {
    for (int i = 0; i < count; ++i) {
        CItem* item = slots[i];
        slots[i] = nullptr;
        if (item) item->Release();
        unit->MarkChanged(kPartEquipment);
    }
}
} // namespace

// The unit prototype table's record named `name`; UnitPrototypeTable::FindByName in
// unit_create.cpp, called through its address.

CItemArmor* CUnit::EquipArmour(CItem* item) {
    if (!item) return nullptr;
    if (x87::fistp(item->condition_) <= 0) return nullptr;   // broken
    CItemArmor* piece = AsArmor(item);
    CItemArmor* previous = armor[piece->GetType()];
    armor[piece->GetType()] = piece;
    ++piece->refCount_;
    Controller()->ArmourPutOn(piece->prototypeId_);
    if (previous) previous->Release();
    MarkChanged(kPartEquipment);
    return previous;
}
EI_REWRITTEN(0x00530730, CUnit::EquipArmour);

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
EI_REWRITTEN(0x00530840, CUnit::UnequipArmour);

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
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(carriedCount); ++i) carried[i]->Release();
    if (carried) {
        gamecrt::operator_delete(carried);
        carried = nullptr;
    }
    carriedCapacity = 0;
    carriedCount = 0;
}
EI_REWRITTEN(0x005315B0, CUnit::ReleaseAllItems);

void CUnit::AddCarried(CItem* item) {
    ++item->refCount_;
    int count = carriedCount;
    int size = count + 1;
    // CArray::SetSize(size), inlined.
    if (size == 0) {
        if (carried) {
            gamecrt::operator_delete(carried);
            carried = nullptr;
        }
        carriedCapacity = 0;
        carriedCount = 0;
    } else if (!carried) {
        carried = gamecrt::operator_new_array<CItem*>(static_cast<std::size_t>(size));
        std::memset(carried, 0, static_cast<std::size_t>(size) * 4);
        carriedCapacity = size;
        carriedCount = size;
    } else if (size <= carriedCapacity) {
        if (size > count) std::memset(carried + count, 0, static_cast<std::size_t>(size - count) * 4);
        carriedCount = size;
    } else {
        int grow = carriedGrowBy;
        if (grow == 0) {
            grow = count / 8;
            if (grow < kCarriedGrowMin) {
                grow = kCarriedGrowMin;
            } else if (grow > kCarriedGrowMax) {
                grow = kCarriedGrowMax;
            }
        }
        int capacity = carriedCapacity + grow;
        if (!(size < capacity)) capacity = size;
        auto* grown = gamecrt::operator_new_array<CItem*>(static_cast<std::size_t>(capacity));
        std::memcpy(grown, carried, static_cast<std::size_t>(carriedCount) * 4);
        std::memset(grown + carriedCount, 0, static_cast<std::size_t>(size - carriedCount) * 4);
        gamecrt::operator_delete(carried);
        carried = grown;
        carriedCount = size;
        carriedCapacity = capacity;
    }
    carried[count] = item;
    MarkChanged(kPartEquipment);
}
EI_REWRITTEN(0x00531220, CUnit::AddCarried);

int CUnit::RemoveCarried(int index) {
    carried[index]->Release();
    int after = carriedCount - index - 1;
    if (after != 0) std::memmove(&carried[index], &carried[index + 1], static_cast<std::size_t>(after) * 4);
    return --carriedCount;
}
EI_REWRITTEN(0x005313B0, CUnit::RemoveCarried);

std::uintptr_t CUnit::SendEquipment() {
    std::uint32_t ids[11];   // 4 weapons, then 7 armour pieces (-1 for none)
    GetEquipmentIds(ids);
    Controller()->SetArmourModels(&ids[4], 7);
    Controller()->SetWeaponModels(&ids[0], 4);
    return 0;
}
EI_REWRITTEN(0x00531110, CUnit::SendEquipment);

std::uint32_t CUnit::SetPrototype23CByName(const char* prototypeName) {
    UnitPrototypeRecord* record = unitdb::PrototypeTable().FindByName(prototypeName);
    if (!record) {
        prototype23C = -1;
        return 0xFFFFFFFFu;
    }
    int offset = unitdb::PrototypeByteOffset(record);
    prototype23C = offset / unitdb::kPrototypeSize;
    // EAX keeps the low half of the IMUL by the reciprocal of 0x120.
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(offset) * 0x38E38E39);
}
EI_REWRITTEN(0x005308D0, CUnit::SetPrototype23CByName);

int CUnit::SetPrototype23CIndex(int index) {
    prototype23C = index;
    return index;
}
EI_REWRITTEN(0x00530920, CUnit::SetPrototype23CIndex);
