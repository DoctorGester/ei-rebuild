// The item classes' virtual slots 13 (values from the prototype record) and 14 (the same item as
// another). See item.h and docs/notes/items_spells.md ("Item rules").
#include "item.h"
#include "spell.h"

#include "ei/hook.h"

#include <cstring>

using namespace itemdb;

namespace {

// FCOMP; TEST AH,40h: ZF is set when the values are equal or unordered (a NaN compares "equal").
bool EqualOrUnordered(float a, float b) { return !(a < b) && !(a > b); }
// The same for a value the item keeps as raw dword bits.
bool EqualOrUnordered(std::uint32_t a, std::uint32_t b) {
    float x, y;
    std::memcpy(&x, &a, 4);
    std::memcpy(&y, &b, 4);
    return EqualOrUnordered(x, y);
}

} // namespace

// --- Slot 21 -----------------------------------------------------------------------------------

ItemUse* CItemQuest::GetUse(ItemUse* out) {
    *out = ItemUse{8, questItemId_, 0};
    return out;
}

// --- Slot 14 -----------------------------------------------------------------------------------

BOOL CItem::IsSameAs(CItem* other) {
    if (other == this) return 1;
    if (type_ != other->type_ || unknown_18 != other->unknown_18) return 0;
    if (!EqualOrUnordered(condition_, other->condition_) || !EqualOrUnordered(conditionMax_, other->conditionMax_) ||
        !EqualOrUnordered(charge_, other->charge_) || !EqualOrUnordered(chargeMax_, other->chargeMax_) ||
        !EqualOrUnordered(weight_, other->weight_) || !EqualOrUnordered(price_, other->price_) ||
        !EqualOrUnordered(unknown_3C, other->unknown_3C))
        return 0;
    if (magicLevel_ != other->magicLevel_) return 0;
    auto* spell = static_cast<const CSpell*>(spell_);
    auto* otherSpell = static_cast<const CSpell*>(other->spell_);
    if (spell == nullptr || otherSpell == nullptr) return spell == otherSpell ? 1 : 0;
    if (spell->unknown_18 != otherSpell->unknown_18) return 0;
    return std::memcmp(spell->modifiers_, otherSpell->modifiers_, sizeof(spell->modifiers_)) == 0 ? 1 : 0;
}

BOOL CItemArmor::IsSameAs(CItem* item) {
    if (!CItem::IsSameAs(item)) return 0;
    auto* other = static_cast<CItemArmor*>(item);
    // Both absorption rows and the layer masks, compared as dwords.
    static_assert(offsetof(CItemArmor, layerMask_) + sizeof(layerMask_) - offsetof(CItemArmor, absorb_) == 0x40);
    return std::memcmp(absorb_, other->absorb_, 0x40) == 0 ? 1 : 0;
}

BOOL CItemQuick::IsSameAs(CItem* other) {
    if (!CItem::IsSameAs(other)) return 0;
    return static_cast<CItemQuick*>(other)->unknown_4C == unknown_4C ? 1 : 0;
}

BOOL CItemQuest::IsSameAs(CItem* other) {
    if (!CItem::IsSameAs(other)) return 0;
    return static_cast<CItemQuest*>(other)->questItemId_ == questItemId_ ? 1 : 0;
}

BOOL CItemLoot::IsSameAs(CItem* item) {
    if (!CItem::IsSameAs(item)) return 0;
    auto* other = static_cast<CItemLoot*>(item);
    if (other->kind_ != kind_ || other->recordType_ != recordType_) return 0;
    return other->index_ == index_ ? 1 : 0;
}

// --- Slot 13 (each inlines CItem's body first) ---------------------------------------------------

void CItemWeapon::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    auto* r = static_cast<const WeaponRecord*>(record);
    std::memcpy(&reach_, &r->reach, 4);
    std::memcpy(&damage_, &r->damage, 4);
    std::memcpy(&damageSpread_, &r->damageSpread, 4);
}

void CItemArmor::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    auto* r = static_cast<const ArmorRecord*>(record);
    std::memcpy(absorb_[0], r->absorbFirst, sizeof(absorb_[0]));
    std::memcpy(absorb_[1], r->absorbOthers, sizeof(absorb_[1]));
}

void CItemQuick::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    unknown_4C = static_cast<const QuickItemRecord*>(record)->value70;
}

void CItemQuest::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    questItemId_ = record->category;
}

void CItemLoot::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    auto* r = static_cast<const LootItemRecord*>(record);
    kind_ = r->category;
    recordType_ = r->recordType;
    index_ = r->index;
}

// --- The virtual slots' entry points (rule 4) -----------------------------------------------------

namespace {
ItemUse* __fastcall QuestGetUseHook(CItemQuest* self, void*, ItemUse* out) { return self->CItemQuest::GetUse(out); }
BOOL __fastcall ItemIsSameAsHook(CItem* self, void*, CItem* other) { return self->CItem::IsSameAs(other); }
BOOL __fastcall ArmorIsSameAsHook(CItemArmor* self, void*, CItem* other) { return self->CItemArmor::IsSameAs(other); }
BOOL __fastcall QuickIsSameAsHook(CItemQuick* self, void*, CItem* other) { return self->CItemQuick::IsSameAs(other); }
BOOL __fastcall QuestIsSameAsHook(CItemQuest* self, void*, CItem* other) { return self->CItemQuest::IsSameAs(other); }
BOOL __fastcall LootIsSameAsHook(CItemLoot* self, void*, CItem* other) { return self->CItemLoot::IsSameAs(other); }
void __fastcall WeaponInitFromRecordHook(CItemWeapon* self, void*, const ItemRecordHead* record) {
    self->CItemWeapon::InitFromRecord(record);
}
void __fastcall ArmorInitFromRecordHook(CItemArmor* self, void*, const ItemRecordHead* record) {
    self->CItemArmor::InitFromRecord(record);
}
void __fastcall QuickInitFromRecordHook(CItemQuick* self, void*, const ItemRecordHead* record) {
    self->CItemQuick::InitFromRecord(record);
}
void __fastcall QuestInitFromRecordHook(CItemQuest* self, void*, const ItemRecordHead* record) {
    self->CItemQuest::InitFromRecord(record);
}
void __fastcall LootInitFromRecordHook(CItemLoot* self, void*, const ItemRecordHead* record) {
    self->CItemLoot::InitFromRecord(record);
}
} // namespace
EI_REPLACE(0x006A43C0, QuestGetUseHook);
EI_REPLACE(0x0069FE90, ItemIsSameAsHook);
EI_REPLACE(0x006A30D0, ArmorIsSameAsHook);
EI_REPLACE(0x006A3C20, QuickIsSameAsHook);
EI_REPLACE(0x006A4510, QuestIsSameAsHook);
EI_REPLACE(0x006A4B00, LootIsSameAsHook);
EI_REPLACE(0x006A0660, WeaponInitFromRecordHook);
EI_REPLACE(0x006A3070, ArmorInitFromRecordHook);
EI_REPLACE(0x006A3BE0, QuickInitFromRecordHook);
EI_REPLACE(0x006A44D0, QuestInitFromRecordHook);
EI_REPLACE(0x006A4AB0, LootInitFromRecordHook);
