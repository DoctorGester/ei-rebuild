// Item copies and item lists: CItem::Clone / CopyTo (0069FF70, 0069FFC0) and the subclasses' CopyTo
// (006A0730, 006A3140, 006A3C50, 006A4540, 006A4B50, 006A57F0), the party's item list
// (00661DB0, 00661F30) and the party unit records' four item lists (006608D0-00661000). See
// party.h, item.h, unit.h and docs/notes/items_spells.md ("Area ticket A-H").
#include "base_manager.h"
#include "item.h"
#include "party.h"
#include "spell.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

// A record's item list (data, count, capacity, grow-by) has AlignedPtrArray's layout.
AlignedPtrArray& List(CEngineObject**& data) { return *reinterpret_cast<AlignedPtrArray*>(&data); }
static_assert(offsetof(PartyUnitRecord, weaponGrowBy) - offsetof(PartyUnitRecord, weapons) == 0xC);
static_assert(sizeof(AlignedPtrArray) == 0x10);

// A flag argument the original reads as a byte.
bool ByteFlag(int flag) { return static_cast<std::uint8_t>(flag) != 0; }

// Appends `item` to `list` (the original's inlined SetSize(count + 1) and store).
void Append(AlignedPtrArray& list, void* item) {
    int index = list.size;
    list.SetSize(index + 1, -1);
    list.data[index] = item;
}

// Removes the element at `index`, closing the gap.
void RemoveAt(AlignedPtrArray& list, int index) {
    int after = list.size - index - 1;
    if (after != 0) std::memmove(list.data + index, list.data + index + 1, static_cast<std::size_t>(after) * 4);
    --list.size;
}

// The world's spell manager (the item's manager context, slot 22).
CSpellManager* SpellManagerOf(CItem* item) { return static_cast<CWorld*>(item->ManagerContext())->SpellManager(); }

CItem* AddEntry(CEngineObject**& data, CItem* item) {
    CItem* entry = item->Clone(SpellManagerOf(item));
    ++entry->refCount_;
    Append(List(data), entry);
    return entry;
}

void RemoveEntry(CEngineObject**& data, CItem* entry) {
    AlignedPtrArray& list = List(data);
    for (int i = 0; i < list.size; ++i) {
        if (list.data[i] != entry) continue;
        entry->Release();
        RemoveAt(list, i);
        return;
    }
}

} // namespace

// --- Items --------------------------------------------------------------------------------------

CItem* CItem::Clone(CSpellManager* spells) {
    auto* copy = static_cast<CItem*>(owner_->CreateObject(type_, -1));
    if (CopyToSlot(copy, spells)) return copy;
    ++copy->refCount_;
    copy->Release();
    return nullptr;
}
EI_REPLACE_METHOD(0x0069FF70, CItem::Clone);

BOOL CItem::CopyTo(CItem* target, CSpellManager* spells) {
    if (target->type_ != type_) return 0;
    target->Destroy();
    target->unknown_18 = unknown_18;
    // +0x20..+0x40 copied as dwords.
    std::memcpy(&target->unknown_20, &unknown_20, offsetof(CItem, spell_) - offsetof(CItem, unknown_20));
    auto* previous = static_cast<CEngineObject*>(target->spell_);
    if (spell_) {
        CSpell* spell = static_cast<CSpell*>(spell_)->Clone(spells);
        target->spell_ = spell;
        if (spell) ++spell->refCount_;
    } else {
        target->spell_ = nullptr;
    }
    if (previous) previous->Release();
    target->unknown_48 = 1;
    return 1;
}
EI_REPLACE_METHOD(0x0069FFC0, CItem::CopyTo);

__declspec(naked) BOOL CItem::CallCopyTo(CItem*, CSpellManager*) EI_ORIGINAL(0x0069FFC0)

// The subclasses' slot 15: the base copy (through its address), then their own dwords.
BOOL CItemWeapon::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CallCopyTo(target, spells)) return 0;
    auto* weapon = static_cast<CItemWeapon*>(target);
    std::memcpy(&weapon->unknown_4C, &unknown_4C, sizeof(CItemWeapon) - offsetof(CItemWeapon, unknown_4C));
    return 1;
}
EI_REPLACE_METHOD(0x006A0730, CItemWeapon::CopyTo);

BOOL CItemArmor::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CallCopyTo(target, spells)) return 0;
    auto* armor = static_cast<CItemArmor*>(target);
    std::memcpy(armor->absorb_, absorb_, sizeof(absorb_));
    armor->layerMask_[0] = layerMask_[0];
    armor->layerMask_[1] = layerMask_[1];
    return 1;
}
EI_REPLACE_METHOD(0x006A3140, CItemArmor::CopyTo);

BOOL CItemQuick::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CallCopyTo(target, spells)) return 0;
    static_cast<CItemQuick*>(target)->unknown_4C = unknown_4C;
    return 1;
}
EI_REPLACE_METHOD(0x006A3C50, CItemQuick::CopyTo);

BOOL CItemQuest::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CallCopyTo(target, spells)) return 0;
    static_cast<CItemQuest*>(target)->unknown_4C = unknown_4C;
    return 1;
}
EI_REPLACE_METHOD(0x006A4540, CItemQuest::CopyTo);

BOOL CItemLoot::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CallCopyTo(target, spells)) return 0;
    auto* loot = static_cast<CItemLoot*>(target);
    loot->kind_ = kind_;
    loot->recordType_ = recordType_;
    loot->index_ = index_;
    return 1;
}
EI_REPLACE_METHOD(0x006A4B50, CItemLoot::CopyTo);

BOOL CItemSpell::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CallCopyTo(target, spells)) return 0;
    static_cast<CItemSpell*>(target)->unknown_4C = unknown_4C;
    return 1;
}
EI_REPLACE_METHOD(0x006A57F0, CItemSpell::CopyTo);

// --- The party's items --------------------------------------------------------------------------

CItem* CParty::AddItem(CItem* item, int keepCount, int merge) {
    if (!item) return nullptr;
    int count = ByteFlag(keepCount) ? static_cast<int>(item->unknown_48) : 1;
    if (ByteFlag(merge)) {
        for (int i = 0; i < items_.size; ++i) {
            auto* existing = static_cast<CItem*>(items_.data[i]);
            if (!existing->IsSameAsSlot(item)) continue;
            existing->unknown_48 += static_cast<std::uint32_t>(count);
            return existing;
        }
    }
    CItem* copy = CopyItem(item);
    copy->unknown_48 = static_cast<std::uint32_t>(count);
    Append(items_, copy);
    ++copy->refCount_;
    return copy;
}
EI_REPLACE_METHOD(0x00661DB0, CParty::AddItem);

CItem* CParty::RemoveItem(CItem* item, int all) {
    for (int i = 0; i < items_.size; ++i) {
        if (items_.data[i] != item) continue;
        auto* entry = static_cast<CItem*>(items_.data[i]);
        if (static_cast<int>(entry->unknown_48) > 1 && !ByteFlag(all)) {
            --entry->unknown_48;
            return entry;
        }
        entry->Release();
        RemoveAt(items_, i);
        return nullptr;
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x00661F30, CParty::RemoveItem);

// --- The party unit records' item lists ---------------------------------------------------------

CItem* PartyUnitRecord::AddWeapon(CItem* item) { return AddEntry(weapons, item); }
EI_REPLACE_METHOD(0x006608D0, PartyUnitRecord::AddWeapon);
CItem* PartyUnitRecord::AddQuickItem(CItem* item) { return AddEntry(quickItems, item); }
EI_REPLACE_METHOD(0x00660AB0, PartyUnitRecord::AddQuickItem);
CItem* PartyUnitRecord::AddArmor(CItem* item) { return AddEntry(armor, item); }
EI_REPLACE_METHOD(0x00660C90, PartyUnitRecord::AddArmor);
CItem* PartyUnitRecord::AddSpell(CItem* item) { return AddEntry(spells, item); }
EI_REPLACE_METHOD(0x00660E70, PartyUnitRecord::AddSpell);

void PartyUnitRecord::RemoveWeapon(CItem* entry) { RemoveEntry(weapons, entry); }
EI_REPLACE_METHOD(0x00660A50, PartyUnitRecord::RemoveWeapon);
void PartyUnitRecord::RemoveQuickItem(CItem* entry) { RemoveEntry(quickItems, entry); }
EI_REPLACE_METHOD(0x00660C30, PartyUnitRecord::RemoveQuickItem);
void PartyUnitRecord::RemoveArmor(CItem* entry) { RemoveEntry(armor, entry); }
EI_REPLACE_METHOD(0x00660E10, PartyUnitRecord::RemoveArmor);
void PartyUnitRecord::RemoveSpell(CItem* entry) { RemoveEntry(spells, entry); }
EI_REPLACE_METHOD(0x00660FF0, PartyUnitRecord::RemoveSpell);
