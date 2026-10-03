// Checks for a unit's own items (unit_items.cpp).
//
// Equipping, releasing and the carried array take and drop references, notify the controller and
// allocate: ours only, with the resulting slots, counts and references checked. The prototype
// index setters change one field: ours, restore, original, compare the field and EAX.
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/game/unit.h"

#include "ei/x87.h"

#include <cstring>

namespace {
void* s_unused;
} // namespace

// 00530730: ours only. A qualifying piece ends in its type's slot; the result is the old piece.
static CEngineObject* __fastcall EquipArmourVerify(CUnit* self, void*, CEngineObject* item) {
    CEngineObject* before[7];
    std::memcpy(before, self->armor, sizeof(before));
    bool qualifies = item && x87::fistp(Field<float>(reinterpret_cast<const char*>(item), 0x20)) > 0;
    int type = qualifies ? static_cast<CItemArmor*>(item)->GetType() : -1;
    CEngineObject* result = self->EquipArmour(item);
    bool ok = qualifies ? self->armor[type] == item && result == before[type]
                        : result == nullptr && std::memcmp(before, self->armor, sizeof(before)) == 0;
    ei::verify::Check("CUnit::EquipArmour", true, ok, "unit %d type %d", self->id_, type);
    return result;
}
EI_VERIFY(0x00530730, EquipArmourVerify, 0, s_unused);

// 00530840: ours only. The slot is empty afterwards and the old piece is returned.
static CEngineObject* __fastcall UnequipArmourVerify(CUnit* self, void*, int slot) {
    CEngineObject* before = self->armor[slot];
    CEngineObject* result = self->UnequipArmour(slot);
    ei::verify::Check("CUnit::UnequipArmour", true, self->armor[slot] == nullptr && result == before, "slot %d", slot);
    return result;
}
EI_VERIFY(0x00530840, UnequipArmourVerify, 0, s_unused);

// 005315B0: ours only. Everything is empty afterwards.
static void __fastcall ReleaseAllItemsVerify(CUnit* self) {
    self->ReleaseAllItems();
    bool empty = self->carried == nullptr && self->carriedCount == 0 && self->carriedCapacity == 0;
    for (CEngineObject* item : self->weapons) empty = empty && !item;
    for (CEngineObject* item : self->quickItems) empty = empty && !item;
    for (CEngineObject* item : self->spells) empty = empty && !item;
    for (CEngineObject* item : self->armor) empty = empty && !item;
    ei::verify::Check("CUnit::ReleaseAllItems", true, empty, "unit %d", self->id_);
}
EI_VERIFY(0x005315B0, ReleaseAllItemsVerify, 0, s_unused);

// 00531220: ours only. The item is appended and referenced once more.
static void __fastcall AddCarriedVerify(CUnit* self, void*, CEngineObject* item) {
    int count = self->carriedCount;
    int refs = item->refCount_;
    self->AddCarried(item);
    bool ok = self->carriedCount == count + 1 && self->carriedCapacity >= self->carriedCount &&
              self->carried[count] == item && item->refCount_ == refs + 1;
    ei::verify::Check("CUnit::AddCarried", true, ok, "count %d", count);
}
EI_VERIFY(0x00531220, AddCarriedVerify, 0, s_unused);

// 005313B0: ours only. One fewer, the following items moved down.
static int __fastcall RemoveCarriedVerify(CUnit* self, void*, int index) {
    int count = self->carriedCount;
    CEngineObject* next = index + 1 < count ? self->carried[index + 1] : nullptr;
    int result = self->RemoveCarried(index);
    bool ok = result == count - 1 && self->carriedCount == count - 1 && (!next || self->carried[index] == next);
    ei::verify::Check("CUnit::RemoveCarried", true, ok, "index %d of %d", index, count);
    return result;
}
EI_VERIFY(0x005313B0, RemoveCarriedVerify, 0, s_unused);

// 00531110: ours only (counted).
static std::uintptr_t __fastcall SendEquipmentVerify(CUnit* self) {
    std::uintptr_t result = self->SendEquipment();
    ei::verify::Check("CUnit::SendEquipment", true, true, "");
    return result;
}
EI_VERIFY(0x00531110, SendEquipmentVerify, 0, s_unused);

// 005308D0: MOV EAX,[ESP+4]; PUSH ESI (5 bytes).
static std::uint32_t(__fastcall* s_setByName)(CUnit*, void*, const char*);
static std::uint32_t __fastcall SetByNameVerify(CUnit* self, void*, const char* name) {
    int saved = self->prototype23C;
    std::uint32_t actual = self->SetPrototype23CByName(name);
    int after = self->prototype23C;
    self->prototype23C = saved;
    std::uint32_t expected = s_setByName(self, nullptr, name);
    ei::verify::Check("CUnit::SetPrototype23CByName", true, expected == actual && after == self->prototype23C,
                      "%s: %d vs %d", name ? name : "(null)", self->prototype23C, after);
    return expected;
}
EI_VERIFY(0x005308D0, SetByNameVerify, 5, s_setByName);

// 00530920: MOV EAX,[ESP+4]; MOV [ECX+0x23C],EAX (10 bytes).
static int(__fastcall* s_setIndex)(CUnit*, void*, int);
static int __fastcall SetIndexVerify(CUnit* self, void*, int index) {
    int actual = self->SetPrototype23CIndex(index);
    int after = self->prototype23C;
    int expected = s_setIndex(self, nullptr, index);
    ei::verify::Check("CUnit::SetPrototype23CIndex", true, expected == actual && after == self->prototype23C, "%d",
                      index);
    return expected;
}
EI_VERIFY(0x00530920, SetIndexVerify, 10, s_setIndex);
