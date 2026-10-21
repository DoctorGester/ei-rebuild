#include "item.h"

#include "ei/hook.h"
#include "random.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <climits>
#include <cstring>

using namespace itemdb;

namespace {
// The game's global ISAAC generator.

constexpr float kZero = 0.0f;   // 0073B858

} // namespace

char* CItem::GetPrototype() {
    std::uint32_t index = PrototypeIndex(prototypeId_);
    switch (type_) {
    case kItemWeapon: return Weapons().RecordAt(index, kWeaponSize);
    case kItemArmor: return Armors().RecordAt(index, kArmorSize);
    case kItemQuick: return QuickItems().RecordAt(index, kQuickItemSize);
    case kItemLoot: return Loot().RecordAt(index, kLootSize);
    case kItemQuest: return QuestItems().RecordAt(index, kQuestItemSize);
    default: return nullptr;
    }
}

int CItem::GetCategory() {
    // Same lookup as GetPrototype, inlined in the original.
    auto* prototype = reinterpret_cast<const ItemRecordHead*>(GetPrototype());
    return prototype ? static_cast<int>(prototype->category) : INT_MAX;
}

int CItem::RunTick() {
    EI_NAN_AUDIT(charge_, chargeMax_);
    if (charge_ == chargeMax_) return 0;
    EI_NAN_AUDIT(chargeRate_);
    if (chargeRate_ <= kZero) {
        std::memcpy(&charge_, &chargeMax_, sizeof(float));
        return 1;
    }
    charge_ = chargeRate_ * chargeMax_ + charge_;
    if (charge_ >= chargeMax_) std::memcpy(&charge_, &chargeMax_, sizeof(float));
    return 1;
}
namespace {
// Slot 6's entry point (rule 4).
int __fastcall ItemRunTickHook(CItem* self) { return self->CItem::RunTick(); }
} // namespace
EI_REPLACE(0x006A0070, ItemRunTickHook);

int CItemWeapon::GetType() {
    return WeaponAt(PrototypeIndex(prototypeId_))->type;
}

void CItemWeapon::GetDamageTypes(float* out) {
    WeaponRecord* prototype = WeaponAt(PrototypeIndex(prototypeId_));
    if (prototype) {
        std::memcpy(out, prototype->damageTypes, 7 * sizeof(float));
        return;
    }
    std::memset(out, 0, 7 * sizeof(float));
}

double CItemWeapon::RollDamage() {
    EI_NAN_AUDIT(damageSpread_);
    if (damageSpread_ == kZero) return damage_;
    float high = damageSpread_ + damage_;   // stored as float in the original
    float low = damage_;                     // read back from memory for the subtraction and the add
    std::uint32_t roll = IsaacRandom::Global().Next();
    volatile std::int64_t roll64 = roll;               // FILD qword: exact
    // 0073C468: the double 0x3DF0000000100000, about 1 / (2^32 - 1).
    constexpr double kRollScale = 0x1.0000000100000p-32;
    // ((high - low) * roll) * scale + low, all on the FPU stack; returned unrounded in ST0.
    return (static_cast<double>(high) - static_cast<double>(low)) * static_cast<double>(roll64) * kRollScale +
           static_cast<double>(low);
}

BOOL CItemWeapon::IsSameAsImpl(CItem* item) {
    if (!CItem::IsSameAsImpl(item)) return 0;
    auto* other = static_cast<CItemWeapon*>(item);
    if (other->actionValue_ != actionValue_) return 0;
    EI_NAN_AUDIT(other->damage_, damage_, other->damageSpread_, damageSpread_, other->reach_, reach_);
    if (other->damage_ != damage_) return 0;
    if (other->damageSpread_ != damageSpread_) return 0;
    if (other->reach_ != reach_) return 0;
    return 1;
}
EI_REPLACE_METHOD(0x006A06C0, CItemWeapon::IsSameAsImpl);

int CItemArmor::GetType() {
    return ArmorAt(PrototypeIndex(prototypeId_))->type;
}

int CItemArmor::GetTypeGroup() {
    int type = ArmorAt(PrototypeIndex(prototypeId_))->type;
    return (type == 0 || type == 1 || type == 2) ? 2 : 1;
}

float CItemLoot::GetPrice() {
    switch (kind_) {
    case 0: return price_;
    case 1: return MaterialAt(index_)->unitPrice;
    case 2: break;
    default: return -1.0f;   // 0073B854
    }
    switch (recordType_) {
    case 0: return WeaponAt(index_)->price;
    case 1: return ArmorAt(index_)->price;
    case 2: return QuickItemAt(index_)->price;
    case 3: return SpellAt(index_)->price;
    case 4: return SpellModifierAt(index_)->price;
    default: return -1.0f;
    }
}
namespace {
// Slot 16's entry point (rule 4).
float __fastcall LootGetPriceHook(CItemLoot* self) { return self->CItemLoot::GetPrice(); }
} // namespace
EI_REPLACE(0x006A5470, LootGetPriceHook);

namespace {
WeaponRecord* WeaponOf(std::uint32_t packedId) { return WeaponAt(PrototypeIndex(packedId)); }
MaterialRecordHead* MaterialOf(std::uint32_t packedId) { return MaterialAt(MaterialIndex(packedId)); }
} // namespace

int __fastcall WeaponType(std::uint32_t packedId) {
    return WeaponOf(packedId)->type;
}

float __fastcall WeaponMinDamage(std::uint32_t packedId) {
    WeaponRecord* weapon = WeaponOf(packedId);
    return weapon->damage * MaterialOf(packedId)->damageFactor;
}

float __fastcall WeaponMaxDamage(std::uint32_t packedId) {
    WeaponRecord* weapon = WeaponOf(packedId);
    return (weapon->damageSpread + weapon->damage) * MaterialOf(packedId)->damageFactor;
}

float __fastcall WeaponAttack(std::uint32_t packedId) {
    WeaponRecord* weapon = WeaponOf(packedId);
    return weapon->value38 * MaterialOf(packedId)->factor38;
}

int __fastcall WeaponDamageType(std::uint32_t packedId) {
    WeaponRecord* weapon = WeaponOf(packedId);
    for (int i = 0; i < 7; ++i) {
        // FCOMP; TEST AH,0x40; JZ: stop at the first weight not equal to zero.
        EI_NAN_AUDIT(weapon->damageTypes[i]);
        if (weapon->damageTypes[i] != kZero) return i;
    }
    return 6;
}

float __fastcall WeaponStat84(std::uint32_t packedId) {
    return WeaponOf(packedId)->attackBonus;
}

float __fastcall WeaponStat88(std::uint32_t packedId) {
    return WeaponOf(packedId)->defenceBonus;
}

int __fastcall ArmorFieldB8(std::uint32_t index) {
    ArmorRecord* armor = ArmorAt(index);
    return armor ? static_cast<int>(armor->layer) : 0;
}
