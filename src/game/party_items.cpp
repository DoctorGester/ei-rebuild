// Item copies and item lists: CItem::Clone / CopyTo (0069FF70, 0069FFC0) and the subclasses' CopyTo
// (006A0730, 006A3140, 006A3C50, 006A4540, 006A4B50, 006A57F0), the party's item list
// (00661DB0, 00661F30) and the party unit records' four item lists (006608D0-00661000). See
// party.h, item.h, unit.h and docs/notes/items_spells.md ("Party item lists").
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

CItem* AddEntry(CItem**& data, CItem* item) {
    CItem* entry = item->Clone(SpellManagerOf(item));
    ++entry->refCount_;
    Append(ItemList(data), entry);
    return entry;
}

void RemoveEntry(CItem**& data, CItem* entry) {
    AlignedPtrArray& list = ItemList(data);
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
    if (CopyTo(copy, spells)) return copy;
    ++copy->refCount_;
    copy->Release();
    return nullptr;
}
EI_REPLACE_METHOD(0x0069FF70, CItem::Clone);

BOOL CItem::CopyToImpl(CItem* target, CSpellManager* spells) {
    if (target->type_ != type_) return 0;
    target->Destroy();
    target->prototypeId_ = prototypeId_;
    // +0x20..+0x40 copied as dwords.
    std::memcpy(&target->condition_, &condition_, offsetof(CItem, spell_) - offsetof(CItem, condition_));
    CSpell* previous = target->spell_;
    if (spell_) {
        CSpell* spell = spell_->Clone(spells);
        target->spell_ = spell;
        if (spell) ++spell->refCount_;
    } else {
        target->spell_ = nullptr;
    }
    if (previous) previous->Release();
    target->count_ = 1;
    return 1;
}


// The subclasses' slot 15: the base copy (0069FFC0), then their own dwords.
BOOL CItemWeapon::CopyToImpl(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyToImpl(target, spells)) return 0;
    auto* weapon = static_cast<CItemWeapon*>(target);
    std::memcpy(&weapon->actionValue_, &actionValue_, sizeof(CItemWeapon) - offsetof(CItemWeapon, actionValue_));
    return 1;
}

BOOL CItemArmor::CopyToImpl(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyToImpl(target, spells)) return 0;
    auto* armor = static_cast<CItemArmor*>(target);
    std::memcpy(armor->absorb_, absorb_, sizeof(absorb_));
    armor->layerMask_[0] = layerMask_[0];
    armor->layerMask_[1] = layerMask_[1];
    return 1;
}

BOOL CItemQuick::CopyToImpl(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyToImpl(target, spells)) return 0;
    static_cast<CItemQuick*>(target)->actionValue_ = actionValue_;
    return 1;
}

BOOL CItemQuest::CopyToImpl(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyToImpl(target, spells)) return 0;
    static_cast<CItemQuest*>(target)->questItemId_ = questItemId_;
    return 1;
}

BOOL CItemLoot::CopyToImpl(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyToImpl(target, spells)) return 0;
    auto* loot = static_cast<CItemLoot*>(target);
    loot->kind_ = kind_;
    loot->recordType_ = recordType_;
    loot->index_ = index_;
    return 1;
}

BOOL CItemSpell::CopyToImpl(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyToImpl(target, spells)) return 0;
    static_cast<CItemSpell*>(target)->actionValue_ = actionValue_;
    return 1;
}

EI_REPLACE_METHOD(0x0069FFC0, CItem::CopyToImpl);
EI_REPLACE_METHOD(0x006A0730, CItemWeapon::CopyToImpl);
EI_REPLACE_METHOD(0x006A3140, CItemArmor::CopyToImpl);
EI_REPLACE_METHOD(0x006A3C50, CItemQuick::CopyToImpl);
EI_REPLACE_METHOD(0x006A4540, CItemQuest::CopyToImpl);
EI_REPLACE_METHOD(0x006A4B50, CItemLoot::CopyToImpl);
EI_REPLACE_METHOD(0x006A57F0, CItemSpell::CopyToImpl);

// --- The party's items --------------------------------------------------------------------------

CItem* CParty::MakeCopyOf(CItem* item) {
    std::uint32_t packed = item->prototypeId_;
    CItem* copy = static_cast<CItemManager*>(ItemManager())->MakeItem(item->type_, packed >> 16, packed & 0xFFFF, -1);
    item->CopyTo(copy, SpellManager());
    return copy;
}
EI_REPLACE_METHOD(0x0055E940, CParty::MakeCopyOf);

CItem* CParty::AddItem(CItem* item, int keepCount, int merge) {
    if (!item) return nullptr;
    int count = ByteFlag(keepCount) ? static_cast<int>(item->count_) : 1;
    if (ByteFlag(merge)) {
        for (int i = 0; i < items_.size; ++i) {
            auto* existing = static_cast<CItem*>(items_.data[i]);
            if (!existing->IsSameAs(item)) continue;
            existing->count_ += static_cast<std::uint32_t>(count);
            return existing;
        }
    }
    CItem* copy = CopyItem(item);
    copy->count_ = static_cast<std::uint32_t>(count);
    Append(items_, copy);
    ++copy->refCount_;
    return copy;
}
EI_REPLACE_METHOD(0x00661DB0, CParty::AddItem);

CItem* CParty::RemoveItem(CItem* item, int all) {
    for (int i = 0; i < items_.size; ++i) {
        if (items_.data[i] != item) continue;
        auto* entry = static_cast<CItem*>(items_.data[i]);
        if (static_cast<int>(entry->count_) > 1 && !ByteFlag(all)) {
            --entry->count_;
            return entry;
        }
        entry->Release();
        RemoveAt(items_, i);
        return nullptr;
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x00661F30, CParty::RemoveItem);

namespace {
constexpr int kQuestItemClassId = 0x14;   // CItemQuest
} // namespace

void CParty::RemoveQuestItems() {
    for (int i = items_.size - 1; i >= 0; --i) {
        auto* item = static_cast<CItem*>(items_.data[i]);
        if (item->GetClassId() == kQuestItemClassId) RemoveItem(item, 1);
    }
}
EI_REPLACE_METHOD(0x00661FB0, CParty::RemoveQuestItems);

void CParty::ReleaseAllItems() {
    for (int i = 0; i < items_.size; ++i) static_cast<CEngineObject*>(items_.data[i])->Release();
    items_.SetSize(0, -1);
}
EI_REPLACE_METHOD(0x00661FE0, CParty::ReleaseAllItems);

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

// --- 00660840 (A-H25): the weight a unit record carries ---------------------------------------------

namespace {
// One running sum over the lists, in the original's order.
void AddListWeight(float& weight, CItem* const* list, std::uint32_t count) {
    for (int i = 0; i < static_cast<int>(count); ++i) {
        if (list[i]) weight += list[i]->Weight();
    }
}
} // namespace

float PartyUnitRecord::CarriedWeight() const {
    float weight = 0.0f;
    AddListWeight(weight, weapons, weaponCount);
    AddListWeight(weight, quickItems, quickItemCount);
    AddListWeight(weight, spells, spellCount);
    AddListWeight(weight, armor, armorCount);
    return weight;
}
EI_REPLACE_METHOD(0x00660840, PartyUnitRecord::CarriedWeight);
