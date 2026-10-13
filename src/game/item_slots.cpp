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

template <typename Record>
const Record* RecordOf(const char* record) { return reinterpret_cast<const Record*>(record); }

} // namespace

// --- Slot 21 -----------------------------------------------------------------------------------

ItemUse* CItemQuest::GetUse(ItemUse* out) {
    *out = ItemUse{8, unknown_4C, 0};
    return out;
}
EI_REPLACE_METHOD(0x006A43C0, CItemQuest::GetUse);

// --- Slot 14 -----------------------------------------------------------------------------------

BOOL CItem::IsSameAs(CItem* other) {
    if (other == this) return 1;
    if (type_ != other->type_ || unknown_18 != other->unknown_18) return 0;
    if (!EqualOrUnordered(unknown_20, other->unknown_20) || !EqualOrUnordered(unknown_24, other->unknown_24) ||
        !EqualOrUnordered(charge_, other->charge_) || !EqualOrUnordered(chargeMax_, other->chargeMax_) ||
        !EqualOrUnordered(unknown_34, other->unknown_34) || !EqualOrUnordered(price_, other->price_) ||
        !EqualOrUnordered(unknown_3C, other->unknown_3C))
        return 0;
    if (unknown_40 != other->unknown_40) return 0;
    auto* spell = static_cast<const CSpell*>(spell_);
    auto* otherSpell = static_cast<const CSpell*>(other->spell_);
    if (spell == nullptr || otherSpell == nullptr) return spell == otherSpell ? 1 : 0;
    if (spell->unknown_18 != otherSpell->unknown_18) return 0;
    return std::memcmp(spell->modifiers_, otherSpell->modifiers_, sizeof(spell->modifiers_)) == 0 ? 1 : 0;
}
EI_REPLACE_METHOD(0x0069FE90, CItem::IsSameAs);

BOOL CItemArmor::IsSameAs(CItemArmor* other) {
    if (!CItem::IsSameAs(other)) return 0;
    // Both absorption rows and the layer masks, compared as dwords.
    static_assert(offsetof(CItemArmor, layerMask_) + sizeof(layerMask_) - offsetof(CItemArmor, absorb_) == 0x40);
    return std::memcmp(absorb_, other->absorb_, 0x40) == 0 ? 1 : 0;
}
EI_REPLACE_METHOD(0x006A30D0, CItemArmor::IsSameAs);

BOOL CItemQuick::IsSameAs(CItemQuick* other) {
    if (!CItem::IsSameAs(other)) return 0;
    return other->unknown_4C == unknown_4C ? 1 : 0;
}
EI_REPLACE_METHOD(0x006A3C20, CItemQuick::IsSameAs);

BOOL CItemQuest::IsSameAs(CItemQuest* other) {
    if (!CItem::IsSameAs(other)) return 0;
    return other->unknown_4C == unknown_4C ? 1 : 0;
}
EI_REPLACE_METHOD(0x006A4510, CItemQuest::IsSameAs);

BOOL CItemLoot::IsSameAs(CItemLoot* other) {
    if (!CItem::IsSameAs(other)) return 0;
    if (other->kind_ != kind_ || other->recordType_ != recordType_) return 0;
    return other->index_ == index_ ? 1 : 0;
}
EI_REPLACE_METHOD(0x006A4B00, CItemLoot::IsSameAs);

// --- Slot 13 (each inlines CItem's body first) ---------------------------------------------------

void CItemWeapon::InitFromRecord(const char* record) {
    CItem::InitFromRecord(record);
    auto* r = RecordOf<WeaponRecord>(record);
    std::memcpy(&unknown_58, &r->value8C, 4);
    std::memcpy(&damage_, &r->damage, 4);
    std::memcpy(&damageSpread_, &r->damageSpread, 4);
}
EI_REPLACE_METHOD(0x006A0660, CItemWeapon::InitFromRecord);

void CItemArmor::InitFromRecord(const char* record) {
    CItem::InitFromRecord(record);
    auto* r = RecordOf<ArmorRecord>(record);
    std::memcpy(absorb_[0], r->absorbFirst, sizeof(absorb_[0]));
    std::memcpy(absorb_[1], r->absorbOthers, sizeof(absorb_[1]));
}
EI_REPLACE_METHOD(0x006A3070, CItemArmor::InitFromRecord);

void CItemQuick::InitFromRecord(const char* record) {
    CItem::InitFromRecord(record);
    unknown_4C = RecordOf<QuickItemRecord>(record)->value70;
}
EI_REPLACE_METHOD(0x006A3BE0, CItemQuick::InitFromRecord);

void CItemQuest::InitFromRecord(const char* record) {
    CItem::InitFromRecord(record);
    unknown_4C = RecordOf<ItemRecordHead>(record)->category;
}
EI_REPLACE_METHOD(0x006A44D0, CItemQuest::InitFromRecord);

void CItemLoot::InitFromRecord(const char* record) {
    CItem::InitFromRecord(record);
    auto* r = RecordOf<LootItemRecord>(record);
    kind_ = r->category;
    recordType_ = r->recordType;
    index_ = r->index;
}
EI_REPLACE_METHOD(0x006A4AB0, CItemLoot::InitFromRecord);
