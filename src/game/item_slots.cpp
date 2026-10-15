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

ItemUse* CItemQuest::GetUseImpl(ItemUse* out) {
    *out = ItemUse{8, questItemId_, 0};
    return out;
}

// --- Slot 14 -----------------------------------------------------------------------------------

BOOL CItem::IsSameAsImpl(CItem* other) {
    if (other == this) return 1;
    if (type_ != other->type_ || prototypeId_ != other->prototypeId_) return 0;
    if (!EqualOrUnordered(condition_, other->condition_) || !EqualOrUnordered(conditionMax_, other->conditionMax_) ||
        !EqualOrUnordered(charge_, other->charge_) || !EqualOrUnordered(chargeMax_, other->chargeMax_) ||
        !EqualOrUnordered(weight_, other->weight_) || !EqualOrUnordered(price_, other->price_) ||
        !EqualOrUnordered(value3C_, other->value3C_))
        return 0;
    if (magicLevel_ != other->magicLevel_) return 0;
    const CSpell* spell = spell_;
    const CSpell* otherSpell = other->spell_;
    if (spell == nullptr || otherSpell == nullptr) return spell == otherSpell ? 1 : 0;
    if (spell->prototypeId_ != otherSpell->prototypeId_) return 0;
    return std::memcmp(spell->modifiers_, otherSpell->modifiers_, sizeof(spell->modifiers_)) == 0 ? 1 : 0;
}

BOOL CItemArmor::IsSameAsImpl(CItem* item) {
    if (!CItem::IsSameAsImpl(item)) return 0;
    auto* other = static_cast<CItemArmor*>(item);
    // Both absorption rows and the layer masks, compared as dwords.
    static_assert(offsetof(CItemArmor, layerMask_) + sizeof(layerMask_) - offsetof(CItemArmor, absorb_) == 0x40);
    return std::memcmp(absorb_, other->absorb_, 0x40) == 0 ? 1 : 0;
}

BOOL CItemQuick::IsSameAsImpl(CItem* other) {
    if (!CItem::IsSameAsImpl(other)) return 0;
    return static_cast<CItemQuick*>(other)->actionValue_ == actionValue_ ? 1 : 0;
}

BOOL CItemQuest::IsSameAsImpl(CItem* other) {
    if (!CItem::IsSameAsImpl(other)) return 0;
    return static_cast<CItemQuest*>(other)->questItemId_ == questItemId_ ? 1 : 0;
}

BOOL CItemLoot::IsSameAsImpl(CItem* item) {
    if (!CItem::IsSameAsImpl(item)) return 0;
    auto* other = static_cast<CItemLoot*>(item);
    if (other->kind_ != kind_ || other->recordType_ != recordType_) return 0;
    return other->index_ == index_ ? 1 : 0;
}

// --- Slot 13 (each inlines CItem's body first) ---------------------------------------------------

void CItemWeapon::InitFromRecordImpl(const ItemRecordHead* record) {
    CItem::InitFromRecordImpl(record);
    auto* r = static_cast<const WeaponRecord*>(record);
    std::memcpy(&reach_, &r->reach, 4);
    std::memcpy(&damage_, &r->damage, 4);
    std::memcpy(&damageSpread_, &r->damageSpread, 4);
}

void CItemArmor::InitFromRecordImpl(const ItemRecordHead* record) {
    CItem::InitFromRecordImpl(record);
    auto* r = static_cast<const ArmorRecord*>(record);
    std::memcpy(absorb_[0], r->absorbFirst, sizeof(absorb_[0]));
    std::memcpy(absorb_[1], r->absorbOthers, sizeof(absorb_[1]));
}

void CItemQuick::InitFromRecordImpl(const ItemRecordHead* record) {
    CItem::InitFromRecordImpl(record);
    actionValue_ = static_cast<const QuickItemRecord*>(record)->value70;
}

void CItemQuest::InitFromRecordImpl(const ItemRecordHead* record) {
    CItem::InitFromRecordImpl(record);
    questItemId_ = record->category;
}

void CItemLoot::InitFromRecordImpl(const ItemRecordHead* record) {
    CItem::InitFromRecordImpl(record);
    auto* r = static_cast<const LootItemRecord*>(record);
    kind_ = r->category;
    recordType_ = r->recordType;
    index_ = r->index;
}

// --- The virtual slots' entry points (rule 4) -----------------------------------------------------

EI_REPLACE_METHOD(0x006A43C0, CItemQuest::GetUseImpl);
EI_REPLACE_METHOD(0x0069FE90, CItem::IsSameAsImpl);
EI_REPLACE_METHOD(0x006A30D0, CItemArmor::IsSameAsImpl);
EI_REPLACE_METHOD(0x006A3C20, CItemQuick::IsSameAsImpl);
EI_REPLACE_METHOD(0x006A4510, CItemQuest::IsSameAsImpl);
EI_REPLACE_METHOD(0x006A4B00, CItemLoot::IsSameAsImpl);
EI_REPLACE_METHOD(0x006A0660, CItemWeapon::InitFromRecordImpl);
EI_REPLACE_METHOD(0x006A3070, CItemArmor::InitFromRecordImpl);
EI_REPLACE_METHOD(0x006A3BE0, CItemQuick::InitFromRecordImpl);
EI_REPLACE_METHOD(0x006A44D0, CItemQuest::InitFromRecordImpl);
EI_REPLACE_METHOD(0x006A4AB0, CItemLoot::InitFromRecordImpl);
