// The item classes' virtuals that take values from the prototype record and that test for the same item as
// another. See item.h and docs/notes/items_spells.md ("Item rules").
#include "item.h"
#include "spell.h"

#include "lib/nan_audit.h"

#include <bit>
#include <cstring>

using namespace itemdb;

namespace {

} // namespace

// --- GetUse -----------------------------------------------------------------------------------

LockKey CItemQuest::GetUse() { return LockKey{8, questItemId_, 0}; }

// --- IsSameAs -----------------------------------------------------------------------------------

BOOL CItem::IsSameAs(CItem* other) {
    if (other == this) return 1;
    if (type_ != other->type_ || prototypeId_ != other->prototypeId_) return 0;
    EI_NAN_AUDIT(condition_, other->condition_, conditionMax_, other->conditionMax_, charge_,
                 other->charge_, chargeMax_, other->chargeMax_, weight_, other->weight_, price_, other->price_,
                 unreadValue_, other->unreadValue_);
    if (condition_ != other->condition_ || conditionMax_ != other->conditionMax_ ||
        charge_ != other->charge_ || chargeMax_ != other->chargeMax_ || weight_ != other->weight_ ||
        price_ != other->price_ || unreadValue_ != other->unreadValue_)
        return 0;
    if (magicLevel_ != other->magicLevel_) return 0;
    const CSpell* spell = spell_;
    const CSpell* otherSpell = other->spell_;
    if (spell == nullptr || otherSpell == nullptr) return spell == otherSpell ? 1 : 0;
    if (spell->prototypeId_ != otherSpell->prototypeId_) return 0;
    return std::memcmp(spell->modifiers_, otherSpell->modifiers_, sizeof(spell->modifiers_)) == 0 ? 1 : 0;
}

BOOL CItemArmor::IsSameAs(CItem* item) {
    if (!CItem::IsSameAs(item)) return 0;
    auto* other = AsArmor(item);
    // Both absorption rows and the layer masks, compared by their bits.
    static_assert(offsetof(CItemArmor, layerMask_) + sizeof(layerMask_) - offsetof(CItemArmor, absorb_) == 0x40);
    return std::memcmp(absorb_, other->absorb_, 0x40) == 0 ? 1 : 0;
}

BOOL CItemQuick::IsSameAs(CItem* other) {
    if (!CItem::IsSameAs(other)) return 0;
    return AsQuick(other)->actionValue_ == actionValue_ ? 1 : 0;
}

BOOL CItemQuest::IsSameAs(CItem* other) {
    if (!CItem::IsSameAs(other)) return 0;
    return AsQuest(other)->questItemId_ == questItemId_ ? 1 : 0;
}

BOOL CItemLoot::IsSameAs(CItem* item) {
    if (!CItem::IsSameAs(item)) return 0;
    auto* other = AsLoot(item);
    if (other->kind_ != kind_ || other->recordType_ != recordType_) return 0;
    return other->index_ == index_ ? 1 : 0;
}

// --- InitFromRecord (each calls CItem's first) ---------------------------------------------------

void CItemWeapon::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    auto* r = RecordAs<WeaponRecord>(record);
    reach_ = r->reach;
    damage_ = r->damage;
    damageSpread_ = r->damageSpread;
}

void CItemArmor::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    auto* r = RecordAs<ArmorRecord>(record);
    std::memcpy(absorb_[0], r->absorbFirst, sizeof(absorb_[0]));
    std::memcpy(absorb_[1], r->absorbOthers, sizeof(absorb_[1]));
}

void CItemQuick::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    actionValue_ = RecordAs<QuickItemRecord>(record)->actionValue;
}

void CItemQuest::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    questItemId_ = record->category;
}

void CItemLoot::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    auto* r = RecordAs<LootItemRecord>(record);
    kind_ = r->category;
    recordType_ = r->recordType;
    index_ = r->index;
}
