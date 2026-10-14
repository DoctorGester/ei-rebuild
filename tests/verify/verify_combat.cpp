// Differential checks for CUnitServer's combat code (combat.cpp).
//
// Pure queries run both versions and compare bit for bit (floats as bits; functions whose callers
// test only AL compare AL). Functions that only fill an output run the original into a scratch copy
// of the output (pre-filled with the caller's bytes) and ours into the real one. Functions that draw
// random numbers or change state run ours, snapshot what they touched (the hit record, the ISAAC
// generator 007A9D40 and the CRT's rand() seed), restore it, run the original and compare; the
// original's results stand. Snapshots are on the stack: the originals call other verified
// functions that do the same.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/item.h"
#include "../../src/game/random.h"

#include <cstring>

namespace {
std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// The CRT's per-thread data (_getptd, 006F00D1); +0x14 is rand()'s seed.
__declspec(naked) char* __cdecl GamePtd() EI_ORIGINAL(0x006F00D1)

// Everything the attack code draws random numbers from.
struct RandomState {
    IsaacRandom isaac;
    std::uint32_t holdrand;

    void Save() {
        std::memcpy(&isaac, reinterpret_cast<const void*>(0x007A9D40), sizeof(isaac));
        std::memcpy(&holdrand, GamePtd() + 0x14, sizeof(holdrand));
    }
    void Restore() const {
        std::memcpy(reinterpret_cast<void*>(0x007A9D40), &isaac, sizeof(isaac));
        std::memcpy(GamePtd() + 0x14, &holdrand, sizeof(holdrand));
    }
    bool SameAs(const RandomState& other) const {
        return holdrand == other.holdrand && std::memcmp(&isaac, &other.isaac, sizeof(isaac)) == 0;
    }
};

int FirstDifference(const void* a, const void* b, int size) {
    for (int i = 0; i < size; ++i)
        if (static_cast<const unsigned char*>(a)[i] != static_cast<const unsigned char*>(b)[i]) return i;
    return -1;
}

int UnitId(const CUnitServer* self) { return self->unit ? self->unit->id_ : -1; }
} // namespace

// --- Pure queries ----------------------------------------------------------------------------

// 0054B620 / 0054B650 / 0054B6C0 / 00550050: PUSH ESI; MOV ESI,ECX; MOV ECX,[ESI+0x1C] (6 bytes).
static float(__fastcall* s_attackRange)(CUnitServer*);
static float __fastcall AttackRangeVerify(CUnitServer* self) {
    float expected = s_attackRange(self);
    float actual = self->AttackRange();
    ei::verify::Check("CUnitServer::AttackRange", Bits(expected), Bits(actual), "unit %d: %g vs %g", UnitId(self),
                      expected, actual);
    return expected;
}
EI_VERIFY(0x0054B620, AttackRangeVerify, 6, s_attackRange);

static int(__fastcall* s_weaponType)(CUnitServer*);
static int __fastcall WeaponTypeVerify(CUnitServer* self) {
    int expected = s_weaponType(self);
    ei::verify::Check("CUnitServer::WeaponType", expected, self->WeaponType(), "unit %d", UnitId(self));
    return expected;
}
EI_VERIFY(0x0054B650, WeaponTypeVerify, 6, s_weaponType);

// 0054B680: MOV ECX,[ECX+0x1C]; PUSH ESI; MOV EAX,[ECX+0x26C] (10 bytes).
static BOOL(__fastcall* s_hasRangedWeapon)(CUnitServer*);
static BOOL __fastcall HasRangedWeaponVerify(CUnitServer* self) {
    BOOL expected = s_hasRangedWeapon(self);
    ei::verify::Check("CUnitServer::HasRangedWeapon", expected, self->HasRangedWeapon(), "unit %d", UnitId(self));
    return expected;
}
EI_VERIFY(0x0054B680, HasRangedWeaponVerify, 10, s_hasRangedWeapon);

static BOOL(__fastcall* s_attacksFromRange)(CUnitServer*);
static BOOL __fastcall AttacksFromRangeVerify(CUnitServer* self) {
    BOOL expected = s_attacksFromRange(self);
    ei::verify::Check("CUnitServer::AttacksFromRange", static_cast<std::uint8_t>(expected),
                      static_cast<std::uint8_t>(self->AttacksFromRange()), "unit %d", UnitId(self));
    return expected;
}
EI_VERIFY(0x0054B6C0, AttacksFromRangeVerify, 6, s_attacksFromRange);

// 0054B760: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static int(__fastcall* s_scaledItemValue)(CUnitServer*, void*, const CItem*);
static int __fastcall ScaledItemValueVerify(CUnitServer* self, void*, const CItem* item) {
    int expected = s_scaledItemValue(self, nullptr, item);
    ei::verify::Check("CUnitServer::ScaledItemValue", expected, self->ScaledItemValue(item), "unit %d item %p",
                      UnitId(self), item);
    return expected;
}
EI_VERIFY(0x0054B760, ScaledItemValueVerify, 6, s_scaledItemValue);

// 0054B9C0: PUSH EBP; MOV EBP,ESP; PUSH ECX; MOV EAX,[ECX+0x210] (10 bytes).
static int(__fastcall* s_scaledAttackValue)(CUnitServer*, void*, int, CItem*);
static int __fastcall ScaledAttackValueVerify(CUnitServer* self, void*, int kind, CItem* item) {
    int expected = s_scaledAttackValue(self, nullptr, kind, item);
    ei::verify::Check("CUnitServer::ScaledAttackValue", expected, self->ScaledAttackValue(kind, item),
                      "unit %d kind %d item %p", UnitId(self), kind, item);
    return expected;
}
EI_VERIFY(0x0054B9C0, ScaledAttackValueVerify, 10, s_scaledAttackValue);

// 0054B580: PUSH ESI; PUSH EDI; MOV EDI,ECX; MOV EAX,[EDI+0x190] (10 bytes). Pure. Rebirth's
// FixPlaingBattleMusicFromAntimagic patches +0x76: with the flag on in a Rebirth launch this compares
// with the patched original.
static bool(__fastcall* s_isFighting)(CUnitServer*);
static bool __fastcall IsFightingVerify(CUnitServer* self) {
    bool expected = s_isFighting(self);
    ei::verify::Check("CUnitServer::IsFighting", expected, self->IsFighting(), "unit %d order %d source %08X",
                      UnitId(self), self->action.kind, self->action.source);
    return expected;
}
EI_VERIFY(0x0054B580, IsFightingVerify, 10, s_isFighting);

// 0054B7C0: MOV EAX,[ESP+4]; MOV ECX,[ECX+0x1C] (7 bytes).
static CItem*(__fastcall* s_spellCarrier)(CUnitServer*, void*, std::uint32_t);
static CItem* __fastcall SpellCarrierVerify(CUnitServer* self, void*, std::uint32_t source) {
    CItem* expected = s_spellCarrier(self, nullptr, source);
    ei::verify::Check("CUnitServer::SpellCarrier", expected, self->SpellCarrier(source), "unit %d source %08X",
                      UnitId(self), source);
    return expected;
}
EI_VERIFY(0x0054B7C0, SpellCarrierVerify, 7, s_spellCarrier);

// 0054B820 / 0054B880: MOV EAX,[ESP+4]; PUSH ESI (5 bytes).
static bool(__fastcall* s_isScrollSource)(CUnitServer*, void*, std::uint32_t);
static bool __fastcall IsScrollSourceVerify(CUnitServer* self, void*, std::uint32_t source) {
    bool expected = s_isScrollSource(self, nullptr, source);
    ei::verify::Check("CUnitServer::IsScrollSource", expected, self->IsScrollSource(source), "unit %d source %08X",
                      UnitId(self), source);
    return expected;
}
EI_VERIFY(0x0054B820, IsScrollSourceVerify, 5, s_isScrollSource);

static void*(__fastcall* s_spellOfSource)(CUnitServer*, void*, std::uint32_t);
static void* __fastcall SpellOfSourceVerify(CUnitServer* self, void*, std::uint32_t source) {
    void* expected = s_spellOfSource(self, nullptr, source);
    ei::verify::Check("CUnitServer::SpellOfSource", expected, self->SpellOfSource(source), "unit %d source %08X",
                      UnitId(self), source);
    return expected;
}
EI_VERIFY(0x0054B880, SpellOfSourceVerify, 5, s_spellOfSource);

// 0054BF80: PUSH ECX; MOV EAX,[ESP+8] (5 bytes).
static bool(__fastcall* s_canCast)(CUnitServer*, void*, const UnitAction*);
static bool __fastcall CanCastVerify(CUnitServer* self, void*, const UnitAction* action) {
    bool expected = s_canCast(self, nullptr, action);
    ei::verify::Check("CUnitServer::CanCast", expected, self->CanCast(action), "unit %d source %08X", UnitId(self),
                      action->source);
    return expected;
}
EI_VERIFY(0x0054BF80, CanCastVerify, 5, s_canCast);

// 0054C070: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static float(__fastcall* s_actionDistance)(CUnitServer*, void*, const UnitAction*);
static float __fastcall ActionDistanceVerify(CUnitServer* self, void*, const UnitAction* action) {
    float expected = s_actionDistance(self, nullptr, action);
    float actual = self->ActionDistance(action);
    ei::verify::Check("CUnitServer::ActionDistance", Bits(expected), Bits(actual), "unit %d kind %d: %g vs %g",
                      UnitId(self), action->kind, expected, actual);
    return expected;
}
EI_VERIFY(0x0054C070, ActionDistanceVerify, 6, s_actionDistance);

// 0054C800: PUSH EBX; PUSH ESI; MOV ESI,[ESP+0xC] (6 bytes).
static bool(__fastcall* s_canReachTarget)(CUnitServer*, void*, const UnitAction*);
static bool __fastcall CanReachTargetVerify(CUnitServer* self, void*, const UnitAction* action) {
    bool expected = s_canReachTarget(self, nullptr, action);
    ei::verify::Check("CUnitServer::CanReachTarget", expected, self->CanReachTarget(action), "unit %d kind %d",
                      UnitId(self), action->kind);
    return expected;
}
EI_VERIFY(0x0054C800, CanReachTargetVerify, 6, s_canReachTarget);

static std::uint32_t(__fastcall* s_weaponTypeMask)(CUnitServer*);
static std::uint32_t __fastcall WeaponTypeMaskVerify(CUnitServer* self) {
    std::uint32_t expected = s_weaponTypeMask(self);
    ei::verify::Check("CUnitServer::WeaponTypeMask", expected, self->WeaponTypeMask(), "unit %d", UnitId(self));
    return expected;
}
EI_VERIFY(0x00550050, WeaponTypeMaskVerify, 6, s_weaponTypeMask);

// 005500A0: MOV EDX,[ESP+4]; MOV EAX,[EDX] (6 bytes).
static std::uint32_t(__fastcall* s_moveModeMask)(CUnitServer*, void*, const MoveModeState*);
static std::uint32_t __fastcall MoveModeMaskVerify(CUnitServer* self, void*, const MoveModeState* mode) {
    std::uint32_t expected = s_moveModeMask(self, nullptr, mode);
    ei::verify::Check("CUnitServer::MoveModeMask", expected, self->MoveModeMask(mode), "unit %d mode %d",
                      UnitId(self), mode->mode);
    return expected;
}
EI_VERIFY(0x005500A0, MoveModeMaskVerify, 6, s_moveModeMask);

// 00556760: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static int(__fastcall* s_level)(CUnitServer*);
static int __fastcall LevelVerify(CUnitServer* self) {
    int expected = s_level(self);
    ei::verify::Check("CUnitServer::Level", expected, self->Level(), "unit %d experience %g", UnitId(self),
                      self->Combat().experience);
    return expected;
}
EI_VERIFY(0x00556760, LevelVerify, 6, s_level);

// --- Output-only -----------------------------------------------------------------------------

// 0054BA30: PUSH EBP; MOV EBP,ESP; SUB ESP,0x24 (6 bytes). Writes out[1..7].
static void(__fastcall* s_damageByType)(CUnitServer*, void*, std::int16_t*);
static void __fastcall DamageByTypeVerify(CUnitServer* self, void*, std::int16_t* out) {
    std::int16_t expected[8];
    std::memcpy(expected, out, sizeof(expected));
    s_damageByType(self, nullptr, expected);
    self->GetDamageByType(out);
    int diff = FirstDifference(expected, out, sizeof(expected));
    ei::verify::Check("CUnitServer::GetDamageByType", -1, diff, "unit %d: first difference at byte %d", UnitId(self),
                      diff);
    std::memcpy(out, expected, sizeof(expected));
}
EI_VERIFY(0x0054BA30, DamageByTypeVerify, 6, s_damageByType);

// 0054BB20: MOV EAX,[ESP+4]; PUSH ESI (5 bytes). Writes 16 bytes.
static void(__fastcall* s_spellDamage)(CUnitServer*, void*, std::uint32_t, std::int16_t*);
static void __fastcall SpellDamageVerify(CUnitServer* self, void*, std::uint32_t source, std::int16_t* out) {
    std::int16_t expected[8];
    std::memcpy(expected, out, sizeof(expected));
    s_spellDamage(self, nullptr, source, expected);
    self->GetSpellDamage(source, out);
    int diff = FirstDifference(expected, out, sizeof(expected));
    ei::verify::Check("CUnitServer::GetSpellDamage", -1, diff, "unit %d source %08X: first difference at byte %d",
                      UnitId(self), source, diff);
    std::memcpy(out, expected, sizeof(expected));
}
EI_VERIFY(0x0054BB20, SpellDamageVerify, 5, s_spellDamage);

// 0054B520: MOV EDX,[ECX+0x190] (6 bytes).
static BOOL(__fastcall* s_movePoint)(CUnitServer*, void*, float*);
static BOOL __fastcall MovePointVerify(CUnitServer* self, void*, float* out) {
    float expected[3];
    std::memcpy(expected, out, sizeof(expected));
    BOOL expectedResult = s_movePoint(self, nullptr, expected);
    BOOL actualResult = self->GetMovePoint(out);
    int diff = FirstDifference(expected, out, sizeof(expected));
    ei::verify::Check("CUnitServer::GetMovePoint", true, expectedResult == actualResult && diff < 0,
                      "unit %d: result %d vs %d, first difference at byte %d", UnitId(self), expectedResult,
                      actualResult, diff);
    std::memcpy(out, expected, sizeof(expected));
    return expectedResult;
}
EI_VERIFY(0x0054B520, MovePointVerify, 6, s_movePoint);

// 00555A00: SUB ESP,8; PUSH EBX; PUSH EBP (5 bytes). Slot 50.
static void(__fastcall* s_attackParams)(CUnitServer*, void*, AttackParams*);
static void __fastcall AttackParamsVerify(CUnitServer* self, void*, AttackParams* out) {
    AttackParams expected;
    std::memcpy(&expected, out, sizeof(expected));
    s_attackParams(self, nullptr, &expected);
    self->GetAttackParams(out);
    int diff = FirstDifference(&expected, out, sizeof(expected));
    ei::verify::Check("CUnitServer::GetAttackParams", -1, diff,
                      "unit %d: first difference at byte %d (attack %d/%d defence %d/%d)", UnitId(self), diff,
                      expected.attack, out->attack, expected.defence, out->defence);
    std::memcpy(out, &expected, sizeof(expected));
}
EI_VERIFY(0x00555A00, AttackParamsVerify, 5, s_attackParams);

// --- State and random numbers ----------------------------------------------------------------

// 00553220: PUSH ESI; MOV ESI,ECX; PUSH 0x20 (5 bytes). Slot 49; changes the unit's flags_.
static void(__fastcall* s_spellBlockFlag)(CUnitServer*);
static void __fastcall SpellBlockFlagVerify(CUnitServer* self) {
    std::uint32_t saved = self->unit->flags_;
    self->UpdateSpellBlockFlag();
    std::uint32_t actual = self->unit->flags_;
    self->unit->flags_ = saved;
    s_spellBlockFlag(self);
    ei::verify::Check("CUnitServer::UpdateSpellBlockFlag", self->unit->flags_, actual, "unit %d", UnitId(self));
}
EI_VERIFY(0x00553220, SpellBlockFlagVerify, 5, s_spellBlockFlag);

// Runs `ours`, keeps the `size` bytes at `fields`, restores them, runs `original` and reports
// whether both left the same bytes (the original's stay).
template <typename Ours, typename Original>
void SameFieldWrites(const char* name, const CUnitServer* self, void* fields, int size, Ours ours,
                     Original original) {
    unsigned char saved[0x40];
    unsigned char actual[0x40];
    std::memcpy(saved, fields, size);
    ours();
    std::memcpy(actual, fields, size);
    std::memcpy(fields, saved, size);
    original();
    int diff = FirstDifference(fields, actual, size);
    ei::verify::Check(name, -1, diff, "unit %d: first difference at byte %d", UnitId(self), diff);
}

// 00552FB0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x10 (6 bytes). Slot 23; writes CombatStats +0x68..+0x6F.
static void(__fastcall* s_sizeClasses)(CUnitServer*);
static void __fastcall SizeClassesVerify(CUnitServer* self) {
    SameFieldWrites("CUnitServer::UpdateSizeClasses", self, &self->Combat().sizeClass, 8,
                    [&] { self->UpdateSizeClasses(); }, [&] { s_sizeClasses(self); });
}
EI_VERIFY(0x00552FB0, SizeClassesVerify, 6, s_sizeClasses);

// 005536B0 (slot 45) starts PUSH ECX; PUSH ESI; MOV ESI,ECX (4 bytes), then CALL 00455940: no
// 5-byte trampoline without a relative call. This stub replays those instructions with an
// absolute call and continues at 005536B9 (which only writes EDX before reading it).
__declspec(naked) static void __fastcall OriginalUpdateMoveFactors(CUnitServer*) {
    __asm {
        push ecx
        push esi
        mov esi, ecx
        mov eax, 0x00455940
        call eax
        mov edx, 0x005536B9
        jmp edx
    }
}
static void* s_unusedMoveFactors;
static void __fastcall MoveFactorsVerify(CUnitServer* self) {
    SameFieldWrites("CUnitServer::UpdateMoveFactors", self, self->moveFactors, 0x20,
                    [&] { self->UpdateMoveFactors(); }, [&] { OriginalUpdateMoveFactors(self); });
}
EI_VERIFY(0x005536B0, MoveFactorsVerify, 0, s_unusedMoveFactors);

// 00555BD0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x50 (6 bytes). Draws from the ISAAC generator.
static float(__fastcall* s_attackRoll)(CUnitServer*, void*, CUnitServer*, bool);
static float __fastcall AttackRollVerify(CUnitServer* self, void*, CUnitServer* attacker, bool backstab) {
    RandomState before, ours;
    before.Save();
    float actual = self->AttackRoll(attacker, backstab);
    ours.Save();
    before.Restore();
    float expected = s_attackRoll(self, nullptr, attacker, backstab);
    RandomState original;
    original.Save();
    ei::verify::Check("CUnitServer::AttackRoll", true, Bits(expected) == Bits(actual) && original.SameAs(ours),
                      "unit %d attacker %d backstab %d: %g vs %g, random state %s", UnitId(self), UnitId(attacker),
                      backstab ? 1 : 0, expected, actual, original.SameAs(ours) ? "same" : "differs");
    return expected;
}
EI_VERIFY(0x00555BD0, AttackRollVerify, 6, s_attackRoll);

// 00553DF0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x28 (6 bytes). Changes the hit; draws random numbers.
static BOOL(__fastcall* s_resolveHit)(CUnitServer*, void*, DamageInfo*, CUnit*);
static BOOL __fastcall ResolveHitVerify(CUnitServer* self, void*, DamageInfo* hit, CUnit* attacker) {
    RandomState before, ours, original;
    DamageInfo savedHit, ourHit;
    before.Save();
    std::memcpy(&savedHit, hit, sizeof(savedHit));
    BOOL actual = self->ResolveHit(hit, attacker);
    ours.Save();
    std::memcpy(&ourHit, hit, sizeof(ourHit));
    before.Restore();
    std::memcpy(hit, &savedHit, sizeof(savedHit));
    BOOL expected = s_resolveHit(self, nullptr, hit, attacker);
    original.Save();
    int diff = FirstDifference(hit, &ourHit, sizeof(ourHit));
    ei::verify::Check("CUnitServer::ResolveHit", true, expected == actual && diff < 0 && original.SameAs(ours),
                      "unit %d attacker %d: result %d vs %d, hit differs at byte %d, random state %s", UnitId(self),
                      attacker ? attacker->id_ : -1, expected, actual, diff,
                      original.SameAs(ours) ? "same" : "differs");
    return expected;
}
EI_VERIFY(0x00553DF0, ResolveHitVerify, 6, s_resolveHit);

// 0054F630: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes). Fills `out`; draws from rand() and ISAAC.
static BOOL(__fastcall* s_buildAttackHit)(CUnitServer*, void*, DamageInfo*, int, int, char);
static BOOL __fastcall BuildAttackHitVerify(CUnitServer* self, void*, DamageInfo* out, int backstab, int location,
                                            char calledShot) {
    RandomState before, ours, original;
    DamageInfo savedHit, ourHit;
    before.Save();
    std::memcpy(&savedHit, out, sizeof(savedHit));
    BOOL actual = self->BuildAttackHit(out, backstab, location, calledShot);
    ours.Save();
    std::memcpy(&ourHit, out, sizeof(ourHit));
    before.Restore();
    std::memcpy(out, &savedHit, sizeof(savedHit));
    BOOL expected = s_buildAttackHit(self, nullptr, out, backstab, location, calledShot);
    original.Save();
    int diff = FirstDifference(out, &ourHit, sizeof(ourHit));
    ei::verify::Check("CUnitServer::BuildAttackHit", true, expected == actual && diff < 0 && original.SameAs(ours),
                      "unit %d backstab %d location %d: hit differs at byte %d (amount %g vs %g), random state %s",
                      UnitId(self), backstab, location, diff, out->amount, ourHit.amount,
                      original.SameAs(ours) ? "same" : "differs");
    return expected;
}
EI_VERIFY(0x0054F630, BuildAttackHitVerify, 6, s_buildAttackHit);

// 0054F580: PUSH EBX; PUSH ESI; PUSH EDI; MOV ESI,ECX (5 bytes). Fills pendingHit.
static BOOL(__fastcall* s_prepareAttack)(CUnitServer*, void*, CEngineObject*, int, char);
static BOOL __fastcall PrepareAttackVerify(CUnitServer* self, void*, CEngineObject* target, int location,
                                           char calledShot) {
    RandomState before, ours, original;
    DamageInfo savedHit, ourHit;
    before.Save();
    std::memcpy(&savedHit, &self->pendingHit, sizeof(savedHit));
    BOOL actual = self->PrepareAttack(target, location, calledShot);
    ours.Save();
    std::memcpy(&ourHit, &self->pendingHit, sizeof(ourHit));
    before.Restore();
    std::memcpy(&self->pendingHit, &savedHit, sizeof(savedHit));
    BOOL expected = s_prepareAttack(self, nullptr, target, location, calledShot);
    original.Save();
    int diff = FirstDifference(&self->pendingHit, &ourHit, sizeof(ourHit));
    ei::verify::Check("CUnitServer::PrepareAttack", true, expected == actual && diff < 0 && original.SameAs(ours),
                      "unit %d location %d: hit differs at byte %d (backstab x%g vs x%g), random state %s",
                      UnitId(self), location, diff, self->pendingHit.backstabFactor, ourHit.backstabFactor,
                      original.SameAs(ours) ? "same" : "differs");
    return expected;
}
EI_VERIFY(0x0054F580, PrepareAttackVerify, 5, s_prepareAttack);

// 00553F40 (slot 20) applies damage, kills, logs and changes diplomacy: ours only. Checks: a hit
// that was not resolved (|+0x20| < FLT_MAX) comes back resolved to +-FLT_MAX, and when damage was
// applied (result 1) bit 0x10 of the stats dword at +0x6F8 is set.
static void* s_unusedTakeHit;
static BOOL __fastcall TakeHitVerify(CUnitServer* self, void*, DamageInfo* hit, CUnit* attacker, CUnit* source) {
    std::uint32_t before = hit->attackRoll;
    bool unresolved = (before & 0x7FFFFFFFu) < 0x7F7FFFFFu;
    BOOL result = self->TakeHit(hit, attacker, source);
    std::uint32_t after = hit->attackRoll;
    std::uint32_t statsFlags;
    std::memcpy(&statsFlags, reinterpret_cast<const char*>(&self->stats) + 0x6F8, sizeof(statsFlags));
    bool resolved = !unresolved || after == 0x7F7FFFFFu || after == 0xFF7FFFFFu;
    bool applied = !result || (statsFlags & 0x10);
    ei::verify::Check("CUnitServer::TakeHit", true, resolved && applied,
                      "unit %d attacker %d: result %d, +0x20 %08X -> %08X, stats flags %08X", UnitId(self),
                      attacker ? attacker->id_ : -1, result, before, after, statsFlags);
    return result;
}
EI_VERIFY(0x00553F40, TakeHitVerify, 0, s_unusedTakeHit);
