// Checks for the item copies and item lists (party_items.cpp). All of them create, release or
// move counted objects, so ours runs alone and the wrapper checks the result: the copy's kind and
// values, the list's count and order. Scenarios: ui_camp (the camp's item moves put items into and
// out of the party and the unit records), trader_stock (the stock's items go to 00661DB0),
// party_records, items.
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/game/party.h"
#include "../../src/game/unit.h"

#include <cstddef>
#include <cstring>
#include <vector>

namespace {
void* s_unused;

std::vector<void*> Snapshot(void** data, int count) { return count > 0 ? std::vector<void*>(data, data + count) : std::vector<void*>(); }

// `after` is `before` with `added` appended.
bool Appended(const std::vector<void*>& before, void** data, int count, void* added) {
    if (count != static_cast<int>(before.size()) + 1 || data[count - 1] != added) return false;
    return before.empty() || std::memcmp(before.data(), data, before.size() * sizeof(void*)) == 0;
}

// `after` is `before` without its element `removed` (or unchanged when it had none).
bool Removed(const std::vector<void*>& before, void** data, int count, void* removed) {
    std::vector<void*> expected;
    bool taken = false;
    for (void* p : before) {
        if (!taken && p == removed) {
            taken = true;
            continue;
        }
        expected.push_back(p);
    }
    return count == static_cast<int>(expected.size()) &&
           (expected.empty() || std::memcmp(expected.data(), data, expected.size() * sizeof(void*)) == 0);
}

// The values CopyTo gives a target.
bool SameValues(const CItem* a, const CItem* b) {
    return a->type_ == b->type_ && a->unknown_18 == b->unknown_18 &&
           std::memcmp(&a->unknown_20, &b->unknown_20, offsetof(CItem, spell_) - offsetof(CItem, unknown_20)) == 0;
}
} // namespace

// 0069FF70 (stolen 0): a null result, or a new item of the same kind and values, counted once.
static CItem* __fastcall CloneVerify(CItem* self, void*, CSpellManager* spells) {
    CItem* copy = self->Clone(spells);
    bool ok = !copy || (copy != self && SameValues(copy, self) && copy->unknown_48 == 1 &&
                        (copy->spell_ != nullptr) == (self->spell_ != nullptr));
    ei::verify::Check("CItem::Clone", true, ok, "kind %X: copy %p", self->type_, copy);
    return copy;
}
EI_VERIFY(0x0069FF70, CloneVerify, 0, s_unused);

// 0069FFC0 (stolen 0): 0 for another kind (target untouched), else the target has the values.
static BOOL __fastcall CopyToVerify(CItem* self, void*, CItem* target, CSpellManager* spells) {
    int targetKind = target->type_;
    BOOL result = self->CopyTo(target, spells);
    bool ok = targetKind != self->type_
                  ? result == 0
                  : result == 1 && SameValues(target, self) && target->unknown_48 == 1 &&
                        (target->spell_ != nullptr) == (self->spell_ != nullptr);
    ei::verify::Check("CItem::CopyTo", true, ok, "kind %X -> %X: %d", self->type_, targetKind, result);
    return result;
}
EI_VERIFY(0x0069FFC0, CopyToVerify, 0, s_unused);

// 00661DB0 (stolen 0): null for a null item; a merged item keeps the list and gets the count; a
// new copy is appended with the count.
static CItem* __fastcall AddItemVerify(CParty* self, void*, CItem* item, int keepCount, int merge) {
    std::vector<void*> before = Snapshot(self->items_.data, self->items_.size);
    int count = item && (keepCount & 0xFF) ? static_cast<int>(item->unknown_48) : 1;
    CItem* result = self->AddItem(item, keepCount, merge);
    bool ok;
    if (!item) {
        ok = !result;
    } else if (result && self->items_.size == static_cast<int>(before.size())) {
        bool listed = false;
        for (void* p : before) listed = listed || p == result;
        ok = (merge & 0xFF) && listed && std::memcmp(before.data(), self->items_.data, before.size() * sizeof(void*)) == 0;
    } else {
        ok = result && Appended(before, self->items_.data, self->items_.size, result) &&
             static_cast<int>(result->unknown_48) == count;
    }
    ei::verify::Check("CParty::AddItem", true, ok, "item %p keep %d merge %d: %p, %d -> %d items", item, keepCount, merge,
                      result, static_cast<int>(before.size()), self->items_.size);
    return result;
}
EI_VERIFY(0x00661DB0, AddItemVerify, 0, s_unused);

// 00661F30 (stolen 0): a returned item is still listed (its count lowered); otherwise the item is
// no longer in the list.
static CItem* __fastcall RemoveItemVerify(CParty* self, void*, CItem* item, int all) {
    std::vector<void*> before = Snapshot(self->items_.data, self->items_.size);
    CItem* result = self->RemoveItem(item, all);
    bool ok = result ? result == item && self->items_.size == static_cast<int>(before.size())
                     : Removed(before, self->items_.data, self->items_.size, item);
    ei::verify::Check("CParty::RemoveItem", true, ok, "item %p all %d: %p, %d -> %d items", item, all, result,
                      static_cast<int>(before.size()), self->items_.size);
    return result;
}
EI_VERIFY(0x00661F30, RemoveItemVerify, 0, s_unused);

// The record lists (stolen 0): Add appends the returned copy (counted), Remove takes the entry out.
#define RECORD_LIST_VERIFY(Add, Remove, data, count)                                       \
    static CItem* __fastcall Add##Verify(PartyUnitRecord* self, void*, CItem* item) {                            \
        std::vector<void*> before = Snapshot(reinterpret_cast<void**>(self->data), static_cast<int>(self->count)); \
        CItem* entry = self->Add(item);                                                                          \
        bool ok = entry && entry != item && SameValues(entry, item) &&                                           \
                  Appended(before, reinterpret_cast<void**>(self->data), static_cast<int>(self->count), entry);  \
        ei::verify::Check("PartyUnitRecord::" #Add, true, ok, "item %p: %p, %d -> %u", item, entry,              \
                          static_cast<int>(before.size()), self->count);                                         \
        return entry;                                                                                            \
    }                                                                                                            \
    static void __fastcall Remove##Verify(PartyUnitRecord* self, void*, CItem* entry) {                          \
        std::vector<void*> before = Snapshot(reinterpret_cast<void**>(self->data), static_cast<int>(self->count)); \
        self->Remove(entry);                                                                                     \
        bool ok = Removed(before, reinterpret_cast<void**>(self->data), static_cast<int>(self->count), entry);   \
        ei::verify::Check("PartyUnitRecord::" #Remove, true, ok, "entry %p: %d -> %u", entry,                    \
                          static_cast<int>(before.size()), self->count);                                         \
    }

RECORD_LIST_VERIFY(AddWeapon, RemoveWeapon, weapons, weaponCount)
RECORD_LIST_VERIFY(AddQuickItem, RemoveQuickItem, quickItems, quickItemCount)
RECORD_LIST_VERIFY(AddArmor, RemoveArmor, armor, armorCount)
RECORD_LIST_VERIFY(AddSpell, RemoveSpell, spells, spellCount)

EI_VERIFY(0x006608D0, AddWeaponVerify, 0, s_unused);
EI_VERIFY(0x00660A50, RemoveWeaponVerify, 0, s_unused);
EI_VERIFY(0x00660AB0, AddQuickItemVerify, 0, s_unused);
EI_VERIFY(0x00660C30, RemoveQuickItemVerify, 0, s_unused);
EI_VERIFY(0x00660C90, AddArmorVerify, 0, s_unused);
EI_VERIFY(0x00660E10, RemoveArmorVerify, 0, s_unused);
EI_VERIFY(0x00660E70, AddSpellVerify, 0, s_unused);
EI_VERIFY(0x00660FF0, RemoveSpellVerify, 0, s_unused);
