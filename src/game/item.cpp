#include "item.h"

#include "ei/hook.h"
#include "random.h"

#include <climits>
#include <cstring>

using namespace itemdb;

namespace {
// The game's global ISAAC generator.
IsaacRandom* GlobalRandom() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }

constexpr float kZero = 0.0f;   // 0073B858

// FCOMP; TEST AH,0x40: ZF set when equal or unordered (a NaN compares "equal").
bool EqualOrUnordered(float a, float b) { return !(a < b) && !(a > b); }
} // namespace

__declspec(naked) BOOL CItem::IsSameAs(CItem*) EI_ORIGINAL(0x0069FE90)

char* CItem::GetPrototype() {
    std::uint32_t index = PrototypeIndex(unknown_18);
    switch (type_) {
    case kItemWeapon: return Weapons().At(index, kWeaponSize);
    case kItemArmor: return Armors().At(index, kArmorSize);
    case kItemQuick: return QuickItems().At(index, kQuickItemSize);
    case kItemQuest: return QuestItems().At(index, kQuestItemSize);
    case kItemLoot: return Loot().At(index, kLootSize);
    default: return nullptr;
    }
}
EI_REPLACE_METHOD(0x006A0110, CItem::GetPrototype);

int CItem::GetCategory() {
    // Same lookup as GetPrototype, inlined in the original.
    char* prototype = GetPrototype();
    return prototype ? Field<int>(prototype, field::kItemCategory) : INT_MAX;
}
EI_REPLACE_METHOD(0x006A4230, CItem::GetCategory);

BOOL CItem::Recharge() {
    if (EqualOrUnordered(charge_, chargeMax_)) return 0;
    if (!(chargeRate_ > kZero)) {
        std::memcpy(&charge_, &chargeMax_, sizeof(float));
        return 1;
    }
    charge_ = chargeRate_ * chargeMax_ + charge_;
    if (charge_ >= chargeMax_) std::memcpy(&charge_, &chargeMax_, sizeof(float));
    return 1;
}
EI_REPLACE_METHOD(0x006A0070, CItem::Recharge);

int CItemWeapon::GetType() {
    return Field<int>(Weapons().At(PrototypeIndex(unknown_18), kWeaponSize), field::kItemType);
}
EI_REPLACE_METHOD(0x006A07F0, CItemWeapon::GetType);

void CItemWeapon::GetDamageTypes(float* out) {
    char* prototype = Weapons().At(PrototypeIndex(unknown_18), kWeaponSize);
    if (prototype) {
        std::memcpy(out, prototype + field::kWeaponDamageTypes, 7 * sizeof(float));
        return;
    }
    std::memset(out, 0, 7 * sizeof(float));
}
EI_REPLACE_METHOD(0x006A0770, CItemWeapon::GetDamageTypes);

float CItemWeapon::RollDamage() {
    if (EqualOrUnordered(damageSpread_, kZero)) return damage_;
    volatile float high = damageSpread_ + damage_;   // stored as float in the original
    float low = damage_;
    std::uint32_t roll = GlobalRandom()->Next();
    // 0073C468: the double 0x3DF0000000100000, about 1 / (2^32 - 1).
    constexpr double kRollScale = 0x1.0000000100000p-32;
    // Returned unrounded in ST0, like the original.
    return static_cast<float>((high - low) * static_cast<double>(roll) * kRollScale + low);
}
EI_REPLACE_METHOD(0x006A2DE0, CItemWeapon::RollDamage);

BOOL CItemWeapon::IsSameAs(CItemWeapon* other) {
    if (!CItem::IsSameAs(other)) return 0;
    if (other->unknown_4C != unknown_4C) return 0;
    if (!EqualOrUnordered(other->damage_, damage_)) return 0;
    if (!EqualOrUnordered(other->damageSpread_, damageSpread_)) return 0;
    if (!EqualOrUnordered(other->unknown_58, unknown_58)) return 0;
    return 1;
}
EI_REPLACE_METHOD(0x006A06C0, CItemWeapon::IsSameAs);

int CItemArmor::GetType() {
    return Field<int>(Armors().At(PrototypeIndex(unknown_18), kArmorSize), field::kItemType);
}
EI_REPLACE_METHOD(0x006A39E0, CItemArmor::GetType);

int CItemArmor::GetTypeGroup() {
    int type = Field<int>(Armors().At(PrototypeIndex(unknown_18), kArmorSize), field::kItemType);
    return (type == 0 || type == 1 || type == 2) ? 2 : 1;
}
EI_REPLACE_METHOD(0x006A31A0, CItemArmor::GetTypeGroup);

float CItemLoot::GetPrice() {
    switch (kind_) {
    case 0: return price_;
    case 1: return Field<float>(Materials().At(index_, kMaterialSize), field::kMaterialPrice);
    case 2: break;
    default: return -1.0f;   // 0073B854
    }
    switch (recordType_) {
    case 0: return Field<float>(Weapons().At(index_, kWeaponSize), field::kItemPrice);
    case 1: return Field<float>(Armors().At(index_, kArmorSize), field::kItemPrice);
    case 2: return Field<float>(QuickItems().At(index_, kQuickItemSize), field::kItemPrice);
    case 3: return Field<float>(Spells().At(index_, kSpellSize), field::kSpellPrice);
    case 4: return Field<float>(SpellModifiers().At(index_, kSpellModifierSize), field::kModifierPrice);
    default: return -1.0f;
    }
}
EI_REPLACE_METHOD(0x006A5470, CItemLoot::GetPrice);

namespace {
char* WeaponOf(std::uint32_t packedId) { return Weapons().At(PrototypeIndex(packedId), kWeaponSize); }
char* MaterialOf(std::uint32_t packedId) { return Materials().At(MaterialIndex(packedId), kMaterialSize); }
} // namespace

int __fastcall WeaponType(std::uint32_t packedId) {
    return Field<int>(WeaponOf(packedId), field::kItemType);
}
EI_REPLACE(0x006A2B80, WeaponType);

float __fastcall WeaponMinDamage(std::uint32_t packedId) {
    char* weapon = WeaponOf(packedId);
    return Field<float>(weapon, field::kWeaponDamage) * Field<float>(MaterialOf(packedId), field::kMaterialDamage);
}
EI_REPLACE(0x006A2A50, WeaponMinDamage);

float __fastcall WeaponMaxDamage(std::uint32_t packedId) {
    char* weapon = WeaponOf(packedId);
    return (Field<float>(weapon, field::kWeaponDamageSpread) + Field<float>(weapon, field::kWeaponDamage)) *
           Field<float>(MaterialOf(packedId), field::kMaterialDamage);
}
EI_REPLACE(0x006A2AE0, WeaponMaxDamage);

float __fastcall WeaponAttack(std::uint32_t packedId) {
    char* weapon = WeaponOf(packedId);
    return Field<float>(weapon, field::kWeaponAttack) * Field<float>(MaterialOf(packedId), field::kMaterialAttack);
}
EI_REPLACE(0x006A2BD0, WeaponAttack);

int __fastcall WeaponDamageType(std::uint32_t packedId) {
    char* weapon = WeaponOf(packedId);
    for (int i = 0; i < 7; ++i) {
        // FCOMP; TEST AH,0x40; JZ: stop at the first weight not equal (and not NaN) to zero.
        if (!EqualOrUnordered(Field<float>(weapon, field::kWeaponDamageTypes + 4 * i), kZero)) return i;
    }
    return 6;
}
EI_REPLACE(0x006A2C60, WeaponDamageType);

float __fastcall WeaponStat84(std::uint32_t packedId) {
    return Field<float>(WeaponOf(packedId), field::kWeapon84);
}
EI_REPLACE(0x006A2D40, WeaponStat84);

float __fastcall WeaponStat88(std::uint32_t packedId) {
    return Field<float>(WeaponOf(packedId), field::kWeapon88);
}
EI_REPLACE(0x006A2D90, WeaponStat88);

int __fastcall ArmorFieldB8(std::uint32_t index) {
    char* armor = Armors().At(index, kArmorSize);
    return armor ? Field<int>(armor, field::kArmorB8) : 0;
}
EI_REPLACE(0x006A3200, ArmorFieldB8);
