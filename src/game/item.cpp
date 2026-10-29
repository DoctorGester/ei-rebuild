#include "item.h"

#include "random.h"
#include "lib/nan_audit.h"

#include <climits>
#include <cstring>

using namespace itemdb;

namespace {

WeaponRecord* WeaponOf(std::uint32_t packedId) { return WeaponAt(PrototypeIndex(packedId)); }
MaterialRecordHead* MaterialOf(std::uint32_t packedId) { return MaterialAt(MaterialIndex(packedId)); }

} // namespace

ItemRecordHead* PrototypeAt(ItemKind kind, std::uint32_t index) {
    switch (kind) {
    case ItemKind::Weapon: return WeaponAt(index);
    case ItemKind::Armor: return ArmorAt(index);
    case ItemKind::Quick:
    case ItemKind::Spell: return QuickItemAt(index);
    case ItemKind::Loot: return LootAt(index);
    case ItemKind::Quest: return QuestItemAt(index);
    default: return nullptr;
    }
}

ItemRecordHead* CItem::GetPrototype() {
    if (Kind() == ItemKind::Spell) return nullptr;
    return PrototypeAt(Kind(), PrototypeIndex(prototypeId_));
}

QuickItemCategory CItem::GetCategory() {
    const ItemRecordHead* prototype = GetPrototype();
    return prototype ? static_cast<QuickItemCategory>(prototype->category) : QuickItemCategory::NoPrototype;
}

bool CItem::RunTick() {
    EI_NAN_AUDIT(charge_, chargeMax_);
    if (charge_ == chargeMax_) return false;
    EI_NAN_AUDIT(chargeRate_);
    if (chargeRate_ <= 0.0f) {
        charge_ = chargeMax_;
        return true;
    }
    charge_ = chargeRate_ * chargeMax_ + charge_;
    if (charge_ >= chargeMax_) charge_ = chargeMax_;
    return true;
}

int CItemWeapon::GetType() {
    return WeaponAt(PrototypeIndex(prototypeId_))->type;
}

void CItemWeapon::GetDamageTypes(float (&out)[kDamageTypeCount]) {
    if (const WeaponRecord* prototype = WeaponAt(PrototypeIndex(prototypeId_)))
        std::memcpy(out, prototype->damageTypes, sizeof(out));
    else
        std::memset(out, 0, sizeof(out));
}

double CItemWeapon::RollDamage() {
    EI_NAN_AUDIT(damageSpread_);
    if (damageSpread_ == 0.0f) return damage_;
    float high = damageSpread_ + damage_;   // stored as float
    float low = damage_;                     // read back from memory for the subtraction and the add
    std::uint32_t roll = g_random.Next();
    volatile std::int64_t roll64 = roll;
    // About 1 / (2^32 - 1).
    constexpr double kRollScale = 0x1.0000000100000p-32;
    // ((high - low) * roll) * scale + low, all in double.
    return (static_cast<double>(high) - static_cast<double>(low)) * static_cast<double>(roll64) * kRollScale +
           static_cast<double>(low);
}

bool CItemWeapon::IsSameAs(CItem* item) {
    if (!CItem::IsSameAs(item)) return false;
    CItemWeapon* other = AsWeapon(item);   // of this one's type (the base's test)
    if (other->actionValue_ != actionValue_) return false;
    EI_NAN_AUDIT(other->damage_, damage_, other->damageSpread_, damageSpread_, other->reach_, reach_);
    if (other->damage_ != damage_) return false;
    if (other->damageSpread_ != damageSpread_) return false;
    if (other->reach_ != reach_) return false;
    return true;
}

int CItemArmor::GetType() {
    return ArmorAt(PrototypeIndex(prototypeId_))->type;
}

float CItemLoot::GetPrice() {
    switch (kind_) {
    case LootKind::Self: return price_;
    case LootKind::Material: return MaterialAt(index_)->unitPrice;
    case LootKind::Record: break;
    default: return -1.0f;
    }
    switch (recordType_) {
    case LootRecordType::Weapon: return WeaponAt(index_)->price;
    case LootRecordType::Armor: return ArmorAt(index_)->price;
    case LootRecordType::Quick: return QuickItemAt(index_)->price;
    case LootRecordType::Spell: return SpellAt(index_)->price;
    case LootRecordType::Modifier: return SpellModifierAt(index_)->price;
    default: return -1.0f;
    }
}

int WeaponType(std::uint32_t packedId) {
    return WeaponOf(packedId)->type;
}

float WeaponMinDamage(std::uint32_t packedId) {
    WeaponRecord* weapon = WeaponOf(packedId);
    return weapon->damage * MaterialOf(packedId)->damageFactor;
}

float WeaponMaxDamage(std::uint32_t packedId) {
    WeaponRecord* weapon = WeaponOf(packedId);
    return (weapon->damageSpread + weapon->damage) * MaterialOf(packedId)->damageFactor;
}

float WeaponWeight(std::uint32_t packedId) {
    WeaponRecord* weapon = WeaponOf(packedId);
    return weapon->weight * MaterialOf(packedId)->weightFactor;
}

int WeaponDamageType(std::uint32_t packedId) {
    WeaponRecord* weapon = WeaponOf(packedId);
    for (int i = 0; i < kDamageTypeCount; ++i) {
        EI_NAN_AUDIT(weapon->damageTypes[i]);
        if (weapon->damageTypes[i] != 0.0f) return i;
    }
    return kDamageTypeCount - 1;
}

float WeaponAttackBonus(std::uint32_t packedId) {
    return WeaponOf(packedId)->attackBonus;
}

float WeaponDefenceBonus(std::uint32_t packedId) {
    return WeaponOf(packedId)->defenceBonus;
}

int ArmorLayer(std::uint32_t index) {
    ArmorRecord* armor = ArmorAt(index);
    return armor ? static_cast<int>(armor->layer) : 0;
}
