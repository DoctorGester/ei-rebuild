// Checks for the item construction code (item_constructor.cpp) and the spell text parser
// (spell_text.cpp). Functions that only write their own object run ours, restore, run the
// original and compare; the ones that count references, release objects or create them run ours
// only and check the resulting state.
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/game/object.h"
#include "../../src/game/spell.h"

#include <cstring>

namespace {

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// Ours on the object, its bytes saved, the object restored, the original on it; bytes and
// results compared. For functions that write nothing but the object.
template <typename R, typename Ours, typename Original>
R CompareOnObject(const char* name, void* object, std::size_t size, Ours ours, Original original) {
    char before[0x60], after[0x60];
    std::memcpy(before, object, size);
    R actual = ours();
    std::memcpy(after, object, size);
    std::memcpy(object, before, size);
    R expected = original();
    bool same = expected == actual && std::memcmp(after, object, size) == 0;
    ei::verify::Check(name, true, same, "%p", object);
    return expected;
}

} // namespace

// --- CItem ---------------------------------------------------------------------------------------

// 006A0490 writes the item's +0x20..+0x44: ours, bytes saved and restored, original; compared.
// MOV EAX,[ESP+4]; FLD [EAX+0x48] (7 bytes).
static void(__fastcall* s_initFromRecord)(CItem*, void*, const char*);
static void __fastcall InitFromRecordVerify(CItem* self, void*, const char* record) {
    char* bytes = reinterpret_cast<char*>(self) + 0x20;
    char before[0x28], ours[0x28];
    std::memcpy(before, bytes, sizeof(before));
    self->InitFromRecord(record);
    std::memcpy(ours, bytes, sizeof(ours));
    std::memcpy(bytes, before, sizeof(before));
    s_initFromRecord(self, nullptr, record);
    ei::verify::Check("CItem::InitFromRecord", true, std::memcmp(ours, bytes, sizeof(ours)) == 0, "item %08X",
                      self->unknown_18);
}
EI_VERIFY(0x006A0490, InitFromRecordVerify, 7, s_initFromRecord);

// 006A04D0 counts and releases spells: ours only. The spell is attached and the price moved
// from the old spell's to the new one's (the same float operations, repeated here).
static void* s_attachSpell;
static void __fastcall AttachSpellVerify(CItem* self, void*, CSpell* spell) {
    const char* previous = static_cast<const char*>(self->spell_);
    volatile float price = self->price_;
    if (previous) price = price - Field<float>(previous, 0x48);
    if (spell) price = Field<float>(reinterpret_cast<const char*>(spell), 0x48) + price;
    self->AttachSpell(spell);
    bool same = self->spell_ == spell && Bits(self->price_) == Bits(price);
    ei::verify::Check("CItem::AttachSpell", true, same, "item %08X spell %p", self->unknown_18, spell);
}
EI_VERIFY(0x006A04D0, AttachSpellVerify, 0, s_attachSpell);

static void* s_itemSetSpell;
static void __fastcall ItemSetSpellVerify(CItem* self, void*, CSpell* spell) {
    float price = self->price_;
    self->SetSpell(spell);
    ei::verify::Check("CItem::SetSpell", true, self->spell_ == spell && Bits(self->price_) == Bits(price),
                      "item %08X spell %p", self->unknown_18, spell);
}
EI_VERIFY(0x006A0520, ItemSetSpellVerify, 0, s_itemSetSpell);

// --- CItemConstructor ----------------------------------------------------------------------------

namespace {
bool Cleared(const CItemConstructor* self) {
    return !self->manager_ && !self->item_ && !self->material_ && !self->prototype_ && !self->spell_;
}
} // namespace

static void* s_ctorReset;
static BOOL __fastcall CtorResetVerify(CItemConstructor* self) {
    BOOL result = self->Reset();
    ei::verify::Check("CItemConstructor::Reset", true, result == 1 && Cleared(self), "%p", self);
    return result;
}
EI_VERIFY(0x006A5B90, CtorResetVerify, 0, s_ctorReset);

static void* s_ctorDetach;
static void __fastcall CtorDetachVerify(CItemConstructor* self) {
    self->Detach();
    ei::verify::Check("CItemConstructor::Detach", true, Cleared(self), "%p", self);
}
EI_VERIFY(0x006A5BD0, CtorDetachVerify, 0, s_ctorDetach);

static void* s_ctorDestruct;
static void __fastcall CtorDestructVerify(CItemConstructor* self) {
    self->DestructBody();
    bool ok = !self->manager_ && !self->item_ && !self->owner_ &&
              *reinterpret_cast<std::uintptr_t*>(self) == CEngineObject::kVtableAddress;
    ei::verify::Check("CItemConstructor::DestructBody", true, ok, "%p", self);
}
EI_VERIFY(0x006A5B20, CtorDestructVerify, 0, s_ctorDestruct);

static void* s_ctorSetManager;
static void __fastcall CtorSetManagerVerify(CItemConstructor* self, void*, CEngineObject* manager) {
    int count = manager ? manager->refCount_ : 0;
    bool same = self->manager_ == manager;
    self->SetManager(manager);
    bool ok = self->manager_ == manager && (!manager || manager->refCount_ == count + (same ? 0 : 1));
    ei::verify::Check("CItemConstructor::SetManager", true, ok, "%p manager %p", self, manager);
}
EI_VERIFY(0x006A5C00, CtorSetManagerVerify, 0, s_ctorSetManager);

static void* s_ctorSetItem;
static void __fastcall CtorSetItemVerify(CItemConstructor* self, void*, CItem* item) {
    self->SetItem(item);
    bool ok = self->item_ == item && !self->material_ && !self->prototype_;
    ei::verify::Check("CItemConstructor::SetItem", true, ok, "%p item %p", self, item);
}
EI_VERIFY(0x006A5C20, CtorSetItemVerify, 0, s_ctorSetItem);

static void* s_beginCreateItem;
static BOOL __fastcall BeginCreateItemVerify(CItemConstructor* self, void*, int kind, int key) {
    BOOL result = self->BeginCreateItem(kind, key);
    bool ok = !self->item_ || (result == 1 && self->item_->type_ == kind && self->count_ == 1 &&
                               (self->flags_ & 8) && !self->material_ && !self->prototype_);
    ei::verify::Check("CItemConstructor::BeginCreateItem", true, ok, "%p kind %X key %d", self, kind, key);
    return result;
}
EI_VERIFY(0x006A5C50, BeginCreateItemVerify, 0, s_beginCreateItem);

// 006A5CC0 / 006A5CD0 / 006A5CE0 write one field: compared on the object.
// MOV EAX,[ESP+4]; MOV [ECX+n],EAX (7 bytes).
static BOOL(__fastcall* s_setMaterial)(CItemConstructor*, void*, char*);
static BOOL __fastcall SetMaterialVerify(CItemConstructor* self, void*, char* material) {
    return CompareOnObject<BOOL>(
        "CItemConstructor::SetMaterial", self, sizeof(*self), [&] { return self->SetMaterial(material); },
        [&] { return s_setMaterial(self, nullptr, material); });
}
EI_VERIFY(0x006A5CC0, SetMaterialVerify, 7, s_setMaterial);

static BOOL(__fastcall* s_setPrototype)(CItemConstructor*, void*, char*);
static BOOL __fastcall SetPrototypeVerify(CItemConstructor* self, void*, char* prototype) {
    return CompareOnObject<BOOL>(
        "CItemConstructor::SetPrototype", self, sizeof(*self), [&] { return self->SetPrototype(prototype); },
        [&] { return s_setPrototype(self, nullptr, prototype); });
}
EI_VERIFY(0x006A5CD0, SetPrototypeVerify, 7, s_setPrototype);

static BOOL(__fastcall* s_setSpellObject)(CItemConstructor*, void*, CSpell*);
static BOOL __fastcall SetSpellObjectVerify(CItemConstructor* self, void*, CSpell* spell) {
    return CompareOnObject<BOOL>(
        "CItemConstructor::SetSpellObject", self, sizeof(*self), [&] { return self->SetSpellObject(spell); },
        [&] { return s_setSpellObject(self, nullptr, spell); });
}
EI_VERIFY(0x006A5CE0, SetSpellObjectVerify, 7, s_setSpellObject);

static void* s_endCreateItem;
static CItem* __fastcall EndCreateItemVerify(CItemConstructor* self) {
    CItem* item = self->item_;
    int count = item ? item->refCount_ : 0;
    CItem* result = self->EndCreateItem();
    bool ok = !self->item_ && (!result || (result == item && result->refCount_ == count - 1 && !self->spell_ &&
                                           self->count_ == 0 && !(self->flags_ & 8)));
    ei::verify::Check("CItemConstructor::EndCreateItem", true, ok, "%p item %p", self, item);
    return result;
}
EI_VERIFY(0x006A5CF0, EndCreateItemVerify, 0, s_endCreateItem);

static void* s_ctorCancel;
static BOOL __fastcall CtorCancelVerify(CItemConstructor* self) {
    CEngineObject* manager = self->manager_;
    BOOL result = self->Cancel();
    bool ok = result == 1 && self->manager_ == manager && !self->item_ && !self->material_ && !self->prototype_ &&
              !self->spell_;
    ei::verify::Check("CItemConstructor::Cancel", true, ok, "%p", self);
    return result;
}
EI_VERIFY(0x006A5D30, CtorCancelVerify, 0, s_ctorCancel);

// 006A5D60 fills the item through its slot 13 and may create a spell: ours only. The item's
// packed id names the prototype (by its index in the item's table) and the material, the count
// is copied, and a weapon or armor without a material fails.
static void* s_build;
static BOOL __fastcall BuildVerify(CItemConstructor* self) {
    int classId = self->item_->GetClassId();
    BOOL result = self->Build();
    bool ok;
    if (!self->material_ && (classId == 0x11 || classId == 0x12)) {
        ok = result == 0;
    } else {
        const RecordTable* table = nullptr;
        int stride = 0;
        switch (classId) {
        case 0x11: table = &itemdb::Weapons(); stride = itemdb::kWeaponSize; break;
        case 0x12: table = &itemdb::Armors(); stride = itemdb::kArmorSize; break;
        case 0x13:
        case 0x16: table = &itemdb::QuickItems(); stride = itemdb::kQuickItemSize; break;
        case 0x14: table = &itemdb::Loot(); stride = itemdb::kLootSize; break;
        case 0x15: table = &itemdb::QuestItems(); stride = itemdb::kQuestItemSize; break;
        default: break;
        }
        std::uint32_t index = table ? static_cast<std::uint32_t>(static_cast<int>(self->prototype_ - table->begin) / stride) : 0;
        std::uint32_t low = self->material_ ? Field<std::uint32_t>(self->material_, 0x0C) & 0xFFFF : 0;
        ok = result == 1 && self->item_->unknown_18 == (index << 16 | low) &&
             self->item_->unknown_48 == static_cast<std::uint32_t>(self->count_);
    }
    ei::verify::Check("CItemConstructor::Build", true, ok, "class %X id %08X", classId, self->item_->unknown_18);
    return result;
}
EI_VERIFY(0x006A5D60, BuildVerify, 0, s_build);

// --- CSpellManager::CreateSpellFromText ------------------------------------------------------------

// 00689030 creates a spell (and may add names to the maps): ours only. The result is a spell
// whose prototype index is in range.
static void* s_createSpellFromText;
static CSpell* __fastcall CreateSpellFromTextVerify(CSpellManager* self, void*, const char* text) {
    CSpell* spell = self->CreateSpellFromText(text);
    bool ok = spell && (spell->unknown_18 >> 16) <
                           static_cast<std::uint32_t>((itemdb::Spells().end - itemdb::Spells().begin) / itemdb::kSpellSize);
    ei::verify::Check("CSpellManager::CreateSpellFromText", true, ok, "text %.60s", text ? text : "(null)");
    return spell;
}
EI_VERIFY(0x00689030, CreateSpellFromTextVerify, 0, s_createSpellFromText);

// 006AC970 creates objects: ours only. A created item is an item (class 0x11..0x16); one made
// from "spell container[...]" holds a spell.
static void* s_createItem;
static CItem* __fastcall CreateItemVerify(CItemManager* self, void*, const char* name) {
    CItem* item = self->CreateItem(name);
    bool ok = true;
    int cls = 0;
    if (item) {
        cls = item->GetClassId();
        ok = cls >= 0x11 && cls <= 0x16;
        if (_strnicmp(name, "spell container", 15) == 0) ok = ok && item->spell_ != nullptr;
    }
    ei::verify::Check("CItemManager::CreateItem", true, ok, "%.60s -> %p class %X count %d", name ? name : "(null)", item,
                      cls, item ? item->refCount_ : 0);
    return item;
}
EI_VERIFY(0x006AC970, CreateItemVerify, 0, s_createItem);

// 006AD000 creates objects: ours only (A-H20). A made item has the kind asked for, and the
// constructor is detached again. Scenarios: items, trader_stock, ui_camp (the trader stock and the
// camp's constructors make items by index), every new game (the hero's starting items).
static void* s_makeItem;
static CItem* __fastcall MakeItemVerify(CItemManager* self, void*, int kind, std::uint32_t index, std::uint32_t material,
                                        int key) {
    CItem* item = self->CItemManager::MakeItem(kind, index, material, key);
    CItemConstructor* constructor = static_cast<CManagerContext*>(self->context_)->ItemConstructor();
    bool ok = (!item || item->type_ == kind) && !constructor->manager_ && !constructor->item_;
    ei::verify::Check("CItemManager::MakeItem", true, ok, "kind %X index %u material %u -> %p type %X", kind, index,
                      material, item, item ? item->type_ : 0);
    return item;
}
EI_VERIFY(0x006AD000, MakeItemVerify, 0, s_makeItem);

// --- A-H25 ----------------------------------------------------------------------------------------

// 006AD1E0 creates a spell and an item: ours only. A made scroll is a spell container holding a
// spell. Reached by every unit, hero or party record with start spells (new game: the hero's).
static CItem* __fastcall CreateSpellItemVerify(CItemManager* self, void*, const char* spellText, CSpellManager* spells) {
    CItem* item = self->CreateSpellItem(spellText, spells);
    bool ok = !item || (item->type_ == 0x3008 && item->spell_ != nullptr);
    ei::verify::Check("CItemManager::CreateSpellItem", true, ok, "%.60s -> %p type %X", spellText ? spellText : "(null)",
                      item, item ? item->type_ : 0);
    return item;
}
EI_VERIFY(0x006AD1E0, CreateSpellItemVerify, 0, s_makeItem);

// 006AD260 / 006AD2F0 / 006AD380 / 006AD410 only write the array: the original fills a scratch
// array, ours the caller's; sizes and texts compared (MOV ECX,[imm32]: 6 bytes). Reached by
// ClassTemplateFill (004A7080: every new game) and the camp's item lists (00612CF0, ui_camp).
namespace {
using ListNamesFn = void(__fastcall*)(CItemManager*, void*, mfc::CStringArray*);
template <void (CItemManager::*Ours)(mfc::CStringArray*)>
struct ListNamesCheck {
    static inline ListNamesFn original;
    static void __fastcall Verify(CItemManager* self, void*, mfc::CStringArray* names) {
        console::CStringArray expected;
        original(self, nullptr, &expected);
        (self->*Ours)(names);
        bool same = expected.size == names->size;
        for (int i = 0; same && i < expected.size; ++i)
            same = std::strcmp(static_cast<const char*>(expected.data[i]), static_cast<const char*>(names->data[i])) == 0;
        ei::verify::Check("CItemManager name list", true, same, "%d / %d names", expected.size, names->size);
    }
};
} // namespace
EI_VERIFY(0x006AD260, ListNamesCheck<&CItemManager::ListMaterialNames>::Verify, 6,
          ListNamesCheck<&CItemManager::ListMaterialNames>::original);
EI_VERIFY(0x006AD2F0, ListNamesCheck<&CItemManager::ListWeaponNames>::Verify, 6,
          ListNamesCheck<&CItemManager::ListWeaponNames>::original);
EI_VERIFY(0x006AD380, ListNamesCheck<&CItemManager::ListArmorNames>::Verify, 6,
          ListNamesCheck<&CItemManager::ListArmorNames>::original);
EI_VERIFY(0x006AD410, ListNamesCheck<&CItemManager::ListQuickItemNames>::Verify, 6,
          ListNamesCheck<&CItemManager::ListQuickItemNames>::original);

// 006A6480 only writes the two floats (SUB ESP,8; PUSH ESI; MOV ESI,ECX: 6 bytes): the original
// into scratch copies, ours into the caller's. Reached by the trader stock's magic items
// (0069B29A, trader_stock).
static bool(__fastcall* s_magicCapacity)(CItemConstructor*, void*, float*, float*);
static bool __fastcall MagicCapacityVerify(CItemConstructor* self, void*, float* level, float* power) {
    float expectedLevel = *level, expectedPower = *power;
    bool expected = s_magicCapacity(self, nullptr, &expectedLevel, &expectedPower);
    bool actual = self->MagicCapacity(level, power);
    ei::verify::Check("CItemConstructor::MagicCapacity", expected, actual, "prototype %p material %p", self->prototype_,
                      self->material_);
    if (expected && actual) {
        ei::verify::CheckNear("CItemConstructor::MagicCapacity level", expectedLevel, *level, "prototype %p",
                              self->prototype_);
        ei::verify::CheckNear("CItemConstructor::MagicCapacity power", expectedPower, *power, "prototype %p",
                              self->prototype_);
    }
    return expected;
}
EI_VERIFY(0x006A6480, MagicCapacityVerify, 6, s_magicCapacity);

// A-H25. 006A5B00, the item constructor's slot 0 (a party's or the world's end): ours only, without
// the free (the wrapper frees): CEngineObject's vtable, no owner, nothing held.
#include "../../src/lib/crt.h"
namespace {
void* s_unusedDelete;
CItemConstructor* __fastcall ConstructorDeleteVerify(CItemConstructor* self, void*, unsigned flags) {
    CItemConstructor* result = self->DeletingDestruct(flags & ~1u);
    std::uintptr_t vtable;
    std::memcpy(&vtable, self, sizeof(vtable));
    const bool ok = result == self && vtable == CEngineObject::kVtableAddress && self->owner_ == nullptr &&
                    self->item_ == nullptr && self->spell_ == nullptr;
    ei::verify::Check("CItemConstructor::DeletingDestruct", true, ok, "constructor %p", static_cast<void*>(self));
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}
} // namespace
EI_VERIFY(0x006A5B00, ConstructorDeleteVerify, 0, s_unusedDelete);
