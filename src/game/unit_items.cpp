// A unit's own items (units outside the party keep them in CUnit; party units' items live in the
// player's records, unit.h): armour, the carried items array, releasing everything, and sending
// the equipment to the controller. See unit.h and docs/notes/units.md.
#include "unit.h"

#include "item.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "originals_units.h"
#include "unit_create.h"

#include <bit>
#include <cstring>

namespace {
constexpr int kPartEquipment = 5;
constexpr int kPartStats = 4;

// Controller slot 27 (ArmourTakenOff), called through the vtable read before the argument.
constexpr int kControllerArmourRemoved = 0x6C / 4;

constexpr int kCarriedGrowMin = 4;
constexpr int kCarriedGrowMax = 0x400;

int ArmourType(CEngineObject* item) { return static_cast<CItemArmor*>(item)->GetType(); }

void ReleaseSlots(CUnit* unit, CEngineObject** slots, int count) {
    for (int i = 0; i < count; ++i) {
        CEngineObject* item = slots[i];
        slots[i] = nullptr;
        if (item) item->Release();
        unit->MarkChanged(kPartEquipment);
    }
}
} // namespace

// 00514930: the unit prototype table's record named `name`; UnitPrototypeTable::FindByName in
// unit_create.cpp, called through its address.

CEngineObject* CUnit::EquipArmour(CEngineObject* item) {
    if (!item) return nullptr;
    if (x87::fistp(std::bit_cast<float>(static_cast<const CItem*>(item)->condition_)) <= 0) return nullptr;   // broken
    CEngineObject* previous = armor[ArmourType(item)];
    armor[ArmourType(item)] = item;
    ++item->refCount_;
    Controller()->ArmourPutOn(item->prototypeId_);
    if (previous) previous->Release();
    MarkChanged(kPartEquipment);
    return previous;
}
EI_REPLACE_METHOD(0x00530730, CUnit::EquipArmour);

CEngineObject* CUnit::UnequipArmour(int slot) {
    CEngineObject* item = armor[slot];
    armor[slot] = nullptr;
    if (item) {
        void* const* vtable = *reinterpret_cast<void* const* const*>(controller);   // read before the call
        int type = ArmourType(item);
        reinterpret_cast<int(__fastcall*)(CEngineObject*, void*, int)>(vtable[kControllerArmourRemoved])(controller,
                                                                                                        nullptr, type);
        item->Release();
    }
    MarkChanged(kPartEquipment);
    MarkChanged(kPartStats);
    return item;
}
EI_REPLACE_METHOD(0x00530840, CUnit::UnequipArmour);

void CUnit::ReleaseAllItems() {
    ReleaseSlots(this, weapons, 4);
    for (CEngineObject*& slot : armor) {
        CEngineObject* item = slot;
        slot = nullptr;
        if (item) {
            void* const* vtable = *reinterpret_cast<void* const* const*>(controller);
            int type = ArmourType(item);
            reinterpret_cast<int(__fastcall*)(CEngineObject*, void*, int)>(vtable[kControllerArmourRemoved])(
                controller, nullptr, type);
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
EI_REPLACE_METHOD(0x005315B0, CUnit::ReleaseAllItems);

void CUnit::AddCarried(CEngineObject* item) {
    ++item->refCount_;
    int count = carriedCount;
    int size = count + 1;
    // CArray::SetSize(size), inlined by the original.
    if (size == 0) {
        if (carried) {
            gamecrt::operator_delete(carried);
            carried = nullptr;
        }
        carriedCapacity = 0;
        carriedCount = 0;
    } else if (!carried) {
        carried = static_cast<CEngineObject**>(gamecrt::operator_new(static_cast<std::size_t>(size) * 4));
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
        auto* grown = static_cast<CEngineObject**>(gamecrt::operator_new(static_cast<std::size_t>(capacity) * 4));
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
EI_REPLACE_METHOD(0x00531220, CUnit::AddCarried);

int CUnit::RemoveCarried(int index) {
    carried[index]->Release();
    int after = carriedCount - index - 1;
    if (after != 0) std::memmove(&carried[index], &carried[index + 1], static_cast<std::size_t>(after) * 4);
    return --carriedCount;
}
EI_REPLACE_METHOD(0x005313B0, CUnit::RemoveCarried);

std::uintptr_t CUnit::SendEquipment() {
    std::uint32_t ids[11];   // 4 weapons, then 7 armour pieces (-1 for none)
    GetEquipmentIds(ids);
    Controller()->SetArmourModels(&ids[4], 7);
    return Controller()->SetWeaponModels(&ids[0], 4);
}
EI_REPLACE_METHOD(0x00531110, CUnit::SendEquipment);

std::uint32_t CUnit::SetPrototype23CByName(const char* prototypeName) {
    char* record = UnitPrototypeFindCopy(&unitdb::PrototypeTable(), nullptr, prototypeName);
    if (!record) {
        prototype23C = -1;
        return 0xFFFFFFFFu;
    }
    int offset = static_cast<int>(record - unitdb::Prototypes().begin);
    prototype23C = offset / unitdb::kPrototypeSize;
    // EAX keeps the low half of the original's IMUL by the reciprocal of 0x120.
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(offset) * 0x38E38E39);
}
EI_REPLACE_METHOD(0x005308D0, CUnit::SetPrototype23CByName);

int CUnit::SetPrototype23CIndex(int index) {
    prototype23C = index;
    return index;
}
EI_REPLACE_METHOD(0x00530920, CUnit::SetPrototype23CIndex);
