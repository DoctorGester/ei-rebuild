// Differential checks for the creature statistics, lock and hit helpers (unit_stats.cpp). All are
// pure or write only their outputs: both versions run and must agree bit for bit.
#include "verify.h"

#include "../../src/game/spell.h"
#include "../../src/game/unit.h"

#include <cstring>

namespace {
std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

std::uint64_t Bits(double d) {
    std::uint64_t u;
    std::memcpy(&u, &d, sizeof(u));
    return u;
}

// A float-returning UnitStats method with one int argument: both versions, bits compared.
template <typename Arg>
float CompareFloat(const char* name, float expected, float actual, Arg arg) {
    ei::verify::Check(name, Bits(expected), Bits(actual), "arg %d: %g vs %g", static_cast<int>(arg), expected, actual);
    return expected;
}
} // namespace

// 00523CD0: FLD [0x0073E124] (6 bytes).
static float(__fastcall* s_period20)(UnitStats*);
static float __fastcall Period20Verify(UnitStats* self) {
    return CompareFloat("UnitStats::Period20", s_period20(self), self->Period20(), 0);
}
EI_VERIFY(0x00523CD0, Period20Verify, 6, s_period20);

// 00525DF0 / 00527030: MOV EDX,[ESP+4]; XOR EAX,EAX (6 bytes).
static float(__fastcall* s_attributeBase)(UnitStats*, void*, int);
static float __fastcall AttributeBaseVerify(UnitStats* self, void*, int index) {
    return CompareFloat("UnitStats::AttributeBase", s_attributeBase(self, nullptr, index), self->AttributeBase(index),
                        index);
}
EI_VERIFY(0x00525DF0, AttributeBaseVerify, 6, s_attributeBase);

static float(__fastcall* s_skillBase)(UnitStats*, void*, int);
static float __fastcall SkillBaseVerify(UnitStats* self, void*, int index) {
    return CompareFloat("UnitStats::SkillBase", s_skillBase(self, nullptr, index), self->SkillBase(index), index);
}
EI_VERIFY(0x00527030, SkillBaseVerify, 6, s_skillBase);

// 00525E10: MOV EAX,[ESP+4]; SUB EAX,0 (7 bytes).
static float(__fastcall* s_attributeWithBonus)(UnitStats*, void*, int);
static float __fastcall AttributeWithBonusVerify(UnitStats* self, void*, int index) {
    return CompareFloat("UnitStats::AttributeWithBonus", s_attributeWithBonus(self, nullptr, index),
                        self->AttributeWithBonus(index), index);
}
EI_VERIFY(0x00525E10, AttributeWithBonusVerify, 7, s_attributeWithBonus);

// 00527050: MOV EDX,[0x007AF26C] (6 bytes).
static float(__fastcall* s_skillWithBonus)(UnitStats*, void*, std::uint32_t);
static float __fastcall SkillWithBonusVerify(UnitStats* self, void*, std::uint32_t index) {
    return CompareFloat("UnitStats::SkillWithBonus", s_skillWithBonus(self, nullptr, index),
                        self->SkillWithBonus(index), index);
}
EI_VERIFY(0x00527050, SkillWithBonusVerify, 6, s_skillWithBonus);

// 00526780: MOV EAX,[ESP+4]; CMP EAX,5 (7 bytes).
static float(__fastcall* s_weaponSkill)(UnitStats*, void*, int);
static float __fastcall WeaponSkillVerify(UnitStats* self, void*, int type) {
    return CompareFloat("UnitStats::WeaponSkill", s_weaponSkill(self, nullptr, type), self->WeaponSkill(type), type);
}
EI_VERIFY(0x00526780, WeaponSkillVerify, 7, s_weaponSkill);

// 005267E0: MOV EDX,[ESP+4]; SUB ESP,8 (7 bytes). The value is exact in ST0; compared as double.
static double(__fastcall* s_skillLevelValue)(UnitStats*, void*, int);
static double __fastcall SkillLevelValueVerify(UnitStats* self, void*, int index) {
    double expected = s_skillLevelValue(self, nullptr, index);
    double actual = self->SkillLevelValue(index);
    ei::verify::Check("UnitStats::SkillLevelValue", Bits(expected), Bits(actual), "skill %d: %g vs %g", index, expected,
                      actual);
    return expected;
}
EI_VERIFY(0x005267E0, SkillLevelValueVerify, 7, s_skillLevelValue);

// 00528270: MOV EDX,[ESP+4]; MOV AL,[EDX+ECX+0x6D3] (11 bytes).
static char*(__fastcall* s_skillLevelRecord)(UnitStats*, void*, int);
static char* __fastcall SkillLevelRecordVerify(UnitStats* self, void*, int index) {
    char* expected = s_skillLevelRecord(self, nullptr, index);
    ei::verify::Check("UnitStats::SkillLevelRecord", expected, self->SkillLevelRecord(index), "skill %d", index);
    return expected;
}
EI_VERIFY(0x00528270, SkillLevelRecordVerify, 11, s_skillLevelRecord);

// 00524280: MOV AL,[ECX+0x6E8] (6 bytes).
static float(__fastcall* s_value6E8)(UnitStats*);
static float __fastcall Value6E8Verify(UnitStats* self) {
    return CompareFloat("UnitStats::Value6E8", s_value6E8(self), self->Value6E8(), self->skillLevels[0x15]);
}
EI_VERIFY(0x00524280, Value6E8Verify, 6, s_value6E8);

// 00525CD0 and its copy 00525D60 (one unused argument): MOV AL,[ECX+0x6E9] (6 bytes).
static float(__fastcall* s_value6E9)(UnitStats*);
static float __fastcall Value6E9Verify(UnitStats* self) {
    return CompareFloat("UnitStats::Value6E9", s_value6E9(self), self->Value6E9(), self->skillLevels[0x16]);
}
EI_VERIFY(0x00525CD0, Value6E9Verify, 6, s_value6E9);

static float(__fastcall* s_value6E9Arg)(UnitStats*, void*, int);
static float __fastcall Value6E9ArgVerify(UnitStats* self, void*, int unused) {
    return CompareFloat("UnitStats::Value6E9Arg", s_value6E9Arg(self, nullptr, unused), self->Value6E9Arg(unused),
                        self->skillLevels[0x16]);
}
EI_VERIFY(0x00525D60, Value6E9ArgVerify, 6, s_value6E9Arg);

// 005259B0 / 00524020: FLD [0x0073B858] (6 bytes).
static float(__fastcall* s_reduce)(UnitStats*, void*, float);
static float __fastcall ReduceVerify(UnitStats* self, void*, float value) {
    float expected = s_reduce(self, nullptr, value);
    float actual = self->Reduce(value);
    ei::verify::Check("UnitStats::Reduce", Bits(expected), Bits(actual), "value %g: %g vs %g", value, expected, actual);
    return expected;
}
EI_VERIFY(0x005259B0, ReduceVerify, 6, s_reduce);

static float(__fastcall* s_reduceKind1)(UnitStats*, void*, float);
static float __fastcall ReduceKind1Verify(UnitStats* self, void*, float value) {
    float expected = s_reduceKind1(self, nullptr, value);
    float actual = self->ReduceKind1(value);
    ei::verify::Check("UnitStats::ReduceKind1", Bits(expected), Bits(actual), "value %g: %g vs %g", value, expected,
                      actual);
    return expected;
}
EI_VERIFY(0x00524020, ReduceKind1Verify, 6, s_reduceKind1);

// 00528840 only writes its two outputs: the original into scratch floats.
// FLD [ECX+0x10]; FSUB [0x0073E10C] (9 bytes).
static void(__fastcall* s_getFactors)(UnitStats*, void*, float*, float*, int);
static void __fastcall GetFactorsVerify(UnitStats* self, void*, float* a, float* b, int unused) {
    float scratch[2];
    float* expectedB = b == a ? &scratch[0] : &scratch[1];
    s_getFactors(self, nullptr, &scratch[0], expectedB, unused);
    self->GetFactors(a, b, unused);
    bool same = Bits(scratch[0]) == Bits(*a) && Bits(*expectedB) == Bits(*b);
    ei::verify::Check("UnitStats::GetFactors", true, same, "%g/%g vs %g/%g", scratch[0], *expectedB, *a, *b);
}
EI_VERIFY(0x00528840, GetFactorsVerify, 9, s_getFactors);

// 00522A80: MOV EDX,[ECX]; PUSH ESI; MOV ESI,[ESP+8] (7 bytes).
static BOOL(__fastcall* s_opens)(LockKey*, void*, const LockKey*);
static BOOL __fastcall OpensVerify(LockKey* self, void*, const LockKey* lock) {
    BOOL expected = s_opens(self, nullptr, lock);
    ei::verify::Check("LockKey::Opens", expected, self->Opens(lock), "key %X/%u/%u lock %X/%u/%u", self->mask, self->id,
                      self->level, lock->mask, lock->id, lock->level);
    return expected;
}
EI_VERIFY(0x00522A80, OpensVerify, 7, s_opens);

// 00522B40 only writes the hit: the original into a scratch DamageInfo.
// MOV EDX,ECX; PUSH EDI; MOV ECX,0xF (8 bytes).
static std::uint32_t(__fastcall* s_resetDamage)(DamageInfo*);
static std::uint32_t __fastcall ResetDamageVerify(DamageInfo* info) {
    DamageInfo expected;
    std::memset(&expected, 0xCD, sizeof(expected));
    std::uint32_t expectedResult = s_resetDamage(&expected);
    std::uint32_t actualResult = ResetDamageInfo(info);
    bool same = expectedResult == actualResult && std::memcmp(&expected, info, sizeof(expected)) == 0;
    ei::verify::Check("ResetDamageInfo", true, same, "result %08X vs %08X", expectedResult, actualResult);
    return expectedResult;
}
EI_VERIFY(0x00522B40, ResetDamageVerify, 8, s_resetDamage);

// 00522B80: FLD [ECX+0xC]; FCOMP [0x0073B858] (9 bytes).
static BOOL(__fastcall* s_hasElemental)(const DamageInfo*);
static BOOL __fastcall HasElementalVerify(const DamageInfo* info) {
    BOOL expected = s_hasElemental(info);
    ei::verify::Check("HasElementalDamage", expected, HasElementalDamage(info), "%08X %08X %08X",
                      Bits(info->typeWeights[3]), Bits(info->typeWeights[4]), Bits(info->typeWeights[5]));
    return expected;
}
EI_VERIFY(0x00522B80, HasElementalVerify, 9, s_hasElemental);
