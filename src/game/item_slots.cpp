// The item classes' virtuals that take values from the prototype record and that test for the same item as
// another. See item.h and docs/notes/items_spells.md ("Item rules").
#include "item.h"
#include "spell.h"

#include "lib/nan_audit.h"

#include <cstring>

using namespace itemdb;

// --- GetUse -----------------------------------------------------------------------------------

LockKey CItemQuest::GetUse() { return LockKey{8, questItemId_, 0}; }

// --- IsSameAs -----------------------------------------------------------------------------------

bool CItem::IsSameAs(CItem* other) {
    if (other == this) return true;
    if (type_ != other->type_ || prototypeId_ != other->prototypeId_) return false;
    EI_NAN_AUDIT(condition_, other->condition_, conditionMax_, other->conditionMax_, charge_,
                 other->charge_, chargeMax_, other->chargeMax_, weight_, other->weight_, price_, other->price_,
                 unreadValue_, other->unreadValue_);
    if (condition_ != other->condition_ || conditionMax_ != other->conditionMax_ ||
        charge_ != other->charge_ || chargeMax_ != other->chargeMax_ || weight_ != other->weight_ ||
        price_ != other->price_ || unreadValue_ != other->unreadValue_)
        return false;
    if (magicLevel_ != other->magicLevel_) return false;
    const CSpell* spell = spell_;
    const CSpell* otherSpell = other->spell_;
    if (spell == nullptr || otherSpell == nullptr) return spell == otherSpell;
    if (spell->prototypeId_ != otherSpell->prototypeId_) return false;
    return std::memcmp(spell->modifiers_, otherSpell->modifiers_, sizeof(spell->modifiers_)) == 0;
}

bool CItemArmor::IsSameAs(CItem* item) {
    if (!CItem::IsSameAs(item)) return false;
    auto* other = AsArmor(item);
    // Both absorption rows and the layer masks, compared by their bits.
    return std::memcmp(absorb_, other->absorb_, sizeof(absorb_)) == 0 &&
           std::memcmp(layerMask_, other->layerMask_, sizeof(layerMask_)) == 0;
}

bool CItemQuick::IsSameAs(CItem* other) {
    if (!CItem::IsSameAs(other)) return false;
    return AsQuick(other)->actionValue_ == actionValue_;
}

bool CItemQuest::IsSameAs(CItem* other) {
    if (!CItem::IsSameAs(other)) return false;
    return AsQuest(other)->questItemId_ == questItemId_;
}

bool CItemLoot::IsSameAs(CItem* item) {
    if (!CItem::IsSameAs(item)) return false;
    auto* other = AsLoot(item);
    if (other->kind_ != kind_ || other->recordType_ != recordType_) return false;
    return other->index_ == index_;
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
    kind_ = static_cast<LootKind>(r->category);
    recordType_ = static_cast<LootRecordType>(r->recordType);
    index_ = r->index;
}
