// Differential checks for the item rules (item.cpp). Most are pure database lookups and formulas:
// both versions run and must agree bit for bit. Functions that write state run ours first on the
// real object, then the original on the restored state, and compare.
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/game/random.h"

#include <cstring>

namespace {
std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}
} // namespace

// --- CItem -----------------------------------------------------------------------------------

// 006A0110: MOV EAX,[ECX+0x10]; PUSH ESI; ADD EAX,-0x3004 (9 bytes).
static char*(__fastcall* s_getPrototype)(CItem*);
static char* __fastcall GetPrototypeVerify(CItem* self) {
    char* expected = s_getPrototype(self);
    ei::verify::Check("CItem::GetPrototype", expected, self->GetPrototype(), "kind %X id %08X", self->type_,
                      self->unknown_18);
    return expected;
}
EI_VERIFY(0x006A0110, GetPrototypeVerify, 9, s_getPrototype);

// 006A4230: same prologue as GetPrototype (9 bytes).
static int(__fastcall* s_getCategory)(CItem*);
static int __fastcall GetCategoryVerify(CItem* self) {
    int expected = s_getCategory(self);
    ei::verify::Check("CItem::GetCategory", expected, self->GetCategory(), "kind %X id %08X", self->type_,
                      self->unknown_18);
    return expected;
}
EI_VERIFY(0x006A4230, GetCategoryVerify, 9, s_getCategory);

// 006A0070 changes charge_: run ours, restore, run the original, compare result and charge.
// FLD [ECX+0x28]; FCOMP [ECX+0x2C] (6 bytes).
static BOOL(__fastcall* s_recharge)(CItem*);
static BOOL __fastcall RechargeVerify(CItem* self) {
    std::uint32_t beforeBits = Bits(self->charge_);
    BOOL actual = self->Recharge();
    std::uint32_t actualCharge = Bits(self->charge_);
    std::memcpy(&self->charge_, &beforeBits, sizeof(float));
    BOOL expected = s_recharge(self);
    bool same = expected == actual && Bits(self->charge_) == actualCharge;
    ei::verify::Check("CItem::Recharge", true, same, "charge %08X max %08X rate %08X: %d/%08X vs %d/%08X", beforeBits,
                      Bits(self->chargeMax_), Bits(self->chargeRate_), expected, Bits(self->charge_), actual,
                      actualCharge);
    return expected;
}
EI_VERIFY(0x006A0070, RechargeVerify, 6, s_recharge);

// --- CItemWeapon -----------------------------------------------------------------------------

// 006A07F0: PUSH ESI; MOV ESI,[0x007C86FC] (7 bytes).
static int(__fastcall* s_weaponGetType)(CItemWeapon*);
static int __fastcall WeaponGetTypeVerify(CItemWeapon* self) {
    int expected = s_weaponGetType(self);
    ei::verify::Check("CItemWeapon::GetType", expected, self->GetType(), "id %08X", self->unknown_18);
    return expected;
}
EI_VERIFY(0x006A07F0, WeaponGetTypeVerify, 7, s_weaponGetType);

// 006A0770 only writes `out`: the original into a scratch array, ours into `out`.
// PUSH EBP; MOV EBP,ESP; PUSH ECX; PUSH ESI (5 bytes).
static void(__fastcall* s_getDamageTypes)(CItemWeapon*, void*, float*);
static void __fastcall GetDamageTypesVerify(CItemWeapon* self, void*, float* out) {
    float expected[7];
    s_getDamageTypes(self, nullptr, expected);
    self->GetDamageTypes(out);
    ei::verify::Check("CItemWeapon::GetDamageTypes", true, std::memcmp(expected, out, sizeof(expected)) == 0,
                      "id %08X", self->unknown_18);
}
EI_VERIFY(0x006A0770, GetDamageTypesVerify, 5, s_getDamageTypes);

// 006A2DE0 draws from the global generator: snapshot it, run ours, restore, run the original,
// compare the roll and the generator state. FLD [ECX+0x54]; FCOMP [0x0073B858] (9 bytes).
static float(__fastcall* s_rollDamage)(CItemWeapon*);
static float __fastcall RollDamageVerify(CItemWeapon* self) {
    auto* rng = reinterpret_cast<IsaacRandom*>(0x007A9D40);
    static IsaacRandom saved, after;
    std::memcpy(&saved, rng, sizeof(saved));
    float actual = self->RollDamage();
    std::memcpy(&after, rng, sizeof(after));
    std::memcpy(rng, &saved, sizeof(saved));
    float expected = s_rollDamage(self);
    bool same = Bits(expected) == Bits(actual) && std::memcmp(&after, rng, sizeof(after)) == 0;
    ei::verify::Check("CItemWeapon::RollDamage", true, same, "damage %g spread %g: %g vs %g", self->damage_,
                      self->damageSpread_, expected, actual);
    return expected;
}
EI_VERIFY(0x006A2DE0, RollDamageVerify, 9, s_rollDamage);

// 006A06C0: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0xC] (6 bytes).
static BOOL(__fastcall* s_isSameAs)(CItemWeapon*, void*, CItemWeapon*);
static BOOL __fastcall IsSameAsVerify(CItemWeapon* self, void*, CItemWeapon* other) {
    BOOL expected = s_isSameAs(self, nullptr, other);
    ei::verify::Check("CItemWeapon::IsSameAs", expected, self->IsSameAs(other), "id %08X vs %08X", self->unknown_18,
                      other->unknown_18);
    return expected;
}
EI_VERIFY(0x006A06C0, IsSameAsVerify, 6, s_isSameAs);

// --- CItemArmor ------------------------------------------------------------------------------

// 006A39E0 and 006A31A0: MOV EDX,[0x007C871C] (6 bytes).
static int(__fastcall* s_armorGetType)(CItemArmor*);
static int __fastcall ArmorGetTypeVerify(CItemArmor* self) {
    int expected = s_armorGetType(self);
    ei::verify::Check("CItemArmor::GetType", expected, self->GetType(), "id %08X", self->unknown_18);
    return expected;
}
EI_VERIFY(0x006A39E0, ArmorGetTypeVerify, 6, s_armorGetType);

static int(__fastcall* s_armorGroup)(CItemArmor*);
static int __fastcall ArmorGroupVerify(CItemArmor* self) {
    int expected = s_armorGroup(self);
    ei::verify::Check("CItemArmor::GetTypeGroup", expected, self->GetTypeGroup(), "id %08X", self->unknown_18);
    return expected;
}
EI_VERIFY(0x006A31A0, ArmorGroupVerify, 6, s_armorGroup);

// --- CItemLoot -------------------------------------------------------------------------------

// 006A5470: MOV EAX,[ECX+0x4C]; PUSH ESI; SUB EAX,0 (7 bytes).
static float(__fastcall* s_lootPrice)(CItemLoot*);
static float __fastcall LootPriceVerify(CItemLoot* self) {
    float expected = s_lootPrice(self);
    ei::verify::Check("CItemLoot::GetPrice", Bits(expected), Bits(self->GetPrice()), "kind %u type %u index %u",
                      self->kind_, self->recordType_, self->index_);
    return expected;
}
EI_VERIFY(0x006A5470, LootPriceVerify, 7, s_lootPrice);

// --- Weapon stats by packed id ---------------------------------------------------------------

// 006A2B80, 006A2C60, 006A2D40, 006A2D90: SHR ECX,0x10; PUSH ESI; PUSH EDI (5 bytes).
// 006A2A50, 006A2AE0, 006A2BD0: PUSH EBX; MOV EBX,[0x007C86F8] (7 bytes).
#define WEAPON_STAT_VERIFY(addr, Fn, R, stolen, toBits)                                                    \
    static R(__fastcall* s_##Fn)(std::uint32_t);                                                         \
    static R __fastcall Fn##Verify(std::uint32_t id) {                                                   \
        R expected = s_##Fn(id);                                                                         \
        ei::verify::Check(#Fn, toBits(expected), toBits(Fn(id)), "id %08X", id);                         \
        return expected;                                                                                 \
    }                                                                                                    \
    EI_VERIFY(addr, Fn##Verify, stolen, s_##Fn)

static int Same(int v) { return v; }
WEAPON_STAT_VERIFY(0x006A2B80, WeaponType, int, 5, Same);
WEAPON_STAT_VERIFY(0x006A2C60, WeaponDamageType, int, 5, Same);
WEAPON_STAT_VERIFY(0x006A2D40, WeaponStat84, float, 5, Bits);
WEAPON_STAT_VERIFY(0x006A2D90, WeaponStat88, float, 5, Bits);
WEAPON_STAT_VERIFY(0x006A2A50, WeaponMinDamage, float, 7, Bits);
WEAPON_STAT_VERIFY(0x006A2AE0, WeaponMaxDamage, float, 7, Bits);
WEAPON_STAT_VERIFY(0x006A2BD0, WeaponAttack, float, 7, Bits);

// 006A3200: MOV EDX,[0x007C871C] (6 bytes).
WEAPON_STAT_VERIFY(0x006A3200, ArmorFieldB8, int, 6, Same);
