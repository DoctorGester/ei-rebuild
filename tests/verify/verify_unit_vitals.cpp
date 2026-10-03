// Differential checks for the statistics in play (unit_vitals.cpp) and their packing and resets
// (unit_stats_pack.cpp).
//
// Pure functions run both versions and compare bit for bit (double results as 64 bits). Functions
// that change the statistics run ours, snapshot what they may write (the stats, 0x704 bytes, and
// the vitals, 0x58), restore, run the original and compare. Functions that only write an output
// record run the original into a scratch copy of it. All buffers are on the stack: the originals
// call other verified functions (HealSlots, ScaleSlotMaxima, the slot factors) that do the same.
#include "verify.h"

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

// Up to two memory blocks a function may write.
struct Block {
    void* at;
    int size;
};

constexpr int kMaxBlock = static_cast<int>(sizeof(UnitStats));

// Runs `ours`, snapshots the blocks, restores them, runs `original`; reports whether both left
// the same bytes and results. `R` is the result type compared (use int for void functions).
template <typename R, typename Ours, typename Original>
R SameBlocks3(const char* name, Block a, Block b, Block c, Ours ours, Original original) {
    const Block blocks[3] = {a, b, c};
    unsigned char saved[3][kMaxBlock];
    unsigned char after[3][kMaxBlock];
    for (int k = 0; k < 3; ++k)
        if (blocks[k].at) std::memcpy(saved[k], blocks[k].at, blocks[k].size);
    R actual = ours();
    for (int k = 0; k < 3; ++k)
        if (blocks[k].at) std::memcpy(after[k], blocks[k].at, blocks[k].size);
    // Restore in reverse order: overlapping blocks get their original bytes either way.
    for (int k = 2; k >= 0; --k)
        if (blocks[k].at) std::memcpy(blocks[k].at, saved[k], blocks[k].size);
    R expected = original();
    int diff[3] = {-1, -1, -1};
    for (int k = 0; k < 3; ++k) {
        for (int i = 0; blocks[k].at && i < blocks[k].size && diff[k] < 0; ++i)
            if (after[k][i] != static_cast<unsigned char*>(blocks[k].at)[i]) diff[k] = i;
    }
    bool sameResult = std::memcmp(&expected, &actual, sizeof(R)) == 0;
    bool same = sameResult && diff[0] < 0 && diff[1] < 0 && diff[2] < 0;
    ei::verify::Check(name, true, same, "first difference: +0x%X / +0x%X / +0x%X; result %s", diff[0], diff[1], diff[2],
                      sameResult ? "same" : "differs");
    return expected;
}

template <typename R, typename Ours, typename Original>
R SameBlocks(const char* name, Block a, Block b, Ours ours, Original original) {
    return SameBlocks3<R>(name, a, b, Block{nullptr, 0}, ours, original);
}

Block StatsBlock(UnitStats* stats) { return {stats, static_cast<int>(sizeof(UnitStats))}; }
Block VitalsBlock(UnitVitals* vitals) { return {vitals, static_cast<int>(sizeof(UnitVitals))}; }
constexpr Block kNone = {nullptr, 0};

// The vitals of the unit's server object, or no block when the object is not one (class 0x39).
Block ServerVitalsBlock(CUnit* unit) {
    auto* server = *reinterpret_cast<CEngineObject**>(reinterpret_cast<char*>(unit) + 0x170);
    if (server->GetClassId() != 0x39) return kNone;
    return {reinterpret_cast<char*>(server) + 0x20, static_cast<int>(sizeof(UnitVitals))};
}
} // namespace

// --- Regeneration and recomputation (stateful) -----------------------------------------------

// 00523740: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes). AL only.
static bool(__fastcall* s_regenerate)(UnitVitals*, void*, UnitStats*, CUnit*, char);
static bool __fastcall RegenerateVerify(UnitVitals* self, void*, UnitStats* stats, CUnit* unit, char inCombat) {
    return SameBlocks<bool>("UnitVitals::Regenerate", VitalsBlock(self), StatsBlock(stats),
                            [&] { return self->Regenerate(stats, unit, inCombat); },
                            [&] { return s_regenerate(self, nullptr, stats, unit, inCombat); });
}
EI_VERIFY(0x00523740, RegenerateVerify, 6, s_regenerate);

// 00524560: PUSH EBP; MOV EBP,ESP; SUB ESP,0xD4 (9 bytes).
static void(__fastcall* s_recompute)(UnitStats*, void*, UnitVitals*, CUnit*);
static void __fastcall RecomputeVerify(UnitStats* self, void*, UnitVitals* vitals, CUnit* unit) {
    SameBlocks<int>("UnitStats::Recompute", StatsBlock(self), VitalsBlock(vitals),
                    [&] { self->Recompute(vitals, unit); return 0; },
                    [&] { s_recompute(self, nullptr, vitals, unit); return 0; });
}
EI_VERIFY(0x00524560, RecomputeVerify, 9, s_recompute);

// 005243D0: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static void(__fastcall* s_recomputeHitPoints)(UnitStats*, void*, UnitVitals*, CUnit*);
static void __fastcall RecomputeHitPointsVerify(UnitStats* self, void*, UnitVitals* vitals, CUnit* unit) {
    SameBlocks<int>("UnitStats::RecomputeHitPoints", StatsBlock(self), VitalsBlock(vitals),
                    [&] { self->RecomputeHitPoints(vitals, unit); return 0; },
                    [&] { s_recomputeHitPoints(self, nullptr, vitals, unit); return 0; });
}
EI_VERIFY(0x005243D0, RecomputeHitPointsVerify, 6, s_recomputeHitPoints);

// 00525800: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes). AL only (the upper bytes of EAX are the
// FPU status word and float bits in the original).
static bool(__fastcall* s_healSlots)(UnitStats*, void*, float, float);
static bool __fastcall HealSlotsVerify(UnitStats* self, void*, float scale, float amount) {
    return SameBlocks<bool>("UnitStats::HealSlots", StatsBlock(self), kNone,
                            [&] { return self->HealSlots(scale, amount); },
                            [&] { return s_healSlots(self, nullptr, scale, amount); });
}
EI_VERIFY(0x00525800, HealSlotsVerify, 6, s_healSlots);

// 00525900: MOV EAX,[ESP+8]; PUSH ESI (5 bytes).
static BOOL(__fastcall* s_heal)(UnitStats*, void*, UnitVitals*, float);
static BOOL __fastcall HealVerify(UnitStats* self, void*, UnitVitals* vitals, float amount) {
    return SameBlocks<BOOL>("UnitStats::Heal", StatsBlock(self), VitalsBlock(vitals),
                            [&] { return self->Heal(vitals, amount); },
                            [&] { return s_heal(self, nullptr, vitals, amount); });
}
EI_VERIFY(0x00525900, HealVerify, 5, s_heal);

// 00525930: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static BOOL(__fastcall* s_regenerateSlots)(UnitStats*, void*, UnitVitals*);
static BOOL __fastcall RegenerateSlotsVerify(UnitStats* self, void*, UnitVitals* vitals) {
    return SameBlocks<BOOL>("UnitStats::RegenerateSlots", StatsBlock(self), VitalsBlock(vitals),
                            [&] { return self->RegenerateSlots(vitals); },
                            [&] { return s_regenerateSlots(self, nullptr, vitals); });
}
EI_VERIFY(0x00525930, RegenerateSlotsVerify, 6, s_regenerateSlots);

// 00525A00 / 00525A70: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes).
static void(__fastcall* s_setSlotMaxima)(UnitStats*, void*, float);
static void __fastcall SetSlotMaximaVerify(UnitStats* self, void*, float value) {
    SameBlocks<int>("UnitStats::SetSlotMaxima", StatsBlock(self), kNone, [&] { self->SetSlotMaxima(value); return 0; },
                    [&] { s_setSlotMaxima(self, nullptr, value); return 0; });
}
EI_VERIFY(0x00525A00, SetSlotMaximaVerify, 6, s_setSlotMaxima);

static void(__fastcall* s_scaleSlotMaxima)(UnitStats*, void*, float);
static void __fastcall ScaleSlotMaximaVerify(UnitStats* self, void*, float value) {
    SameBlocks<int>("UnitStats::ScaleSlotMaxima", StatsBlock(self), kNone,
                    [&] { self->ScaleSlotMaxima(value); return 0; },
                    [&] { s_scaleSlotMaxima(self, nullptr, value); return 0; });
}
EI_VERIFY(0x00525A70, ScaleSlotMaximaVerify, 6, s_scaleSlotMaxima);

// 00525AF0: LEA EAX,[ECX+0x1F8] (6 bytes); 00523C80: LEA EAX,[ECX+0x1F0] (6 bytes).
static void*(__fastcall* s_refillSlots)(UnitStats*);
static void* __fastcall RefillSlotsVerify(UnitStats* self) {
    return SameBlocks<void*>("UnitStats::RefillSlots", StatsBlock(self), kNone, [&] { return self->RefillSlots(); },
                             [&] { return s_refillSlots(self); });
}
EI_VERIFY(0x00525AF0, RefillSlotsVerify, 6, s_refillSlots);

static void*(__fastcall* s_restoreSlots)(UnitStats*);
static void* __fastcall RestoreSlotsVerify(UnitStats* self) {
    return SameBlocks<void*>("UnitStats::RestoreSlots", StatsBlock(self), kNone, [&] { return self->RestoreSlots(); },
                             [&] { return s_restoreSlots(self); });
}
EI_VERIFY(0x00523C80, RestoreSlotsVerify, 6, s_restoreSlots);

// --- Experience (stateful) -------------------------------------------------------------------

// 00523920: FLD [ESP+4]; FADD [ECX+0x700] (10 bytes).
static void(__fastcall* s_addExperience)(UnitStats*, void*, float);
static void __fastcall AddExperienceVerify(UnitStats* self, void*, float amount) {
    SameBlocks<int>("UnitStats::AddExperience", StatsBlock(self), kNone, [&] { self->AddExperience(amount); return 0; },
                    [&] { s_addExperience(self, nullptr, amount); return 0; });
}
EI_VERIFY(0x00523920, AddExperienceVerify, 10, s_addExperience);

// 00523960: FLD qword [0x0073E128] (6 bytes).
static void(__fastcall* s_addScaledExperience)(UnitStats*, void*, float);
static void __fastcall AddScaledExperienceVerify(UnitStats* self, void*, float amount) {
    SameBlocks<int>("UnitStats::AddScaledExperience", StatsBlock(self), kNone,
                    [&] { self->AddScaledExperience(amount); return 0; },
                    [&] { s_addScaledExperience(self, nullptr, amount); return 0; });
}
EI_VERIFY(0x00523960, AddScaledExperienceVerify, 6, s_addScaledExperience);

// 00523C20: FLD [ECX+0x700] (6 bytes).
static void(__fastcall* s_takePending)(UnitStats*, void*, float);
static void __fastcall TakePendingVerify(UnitStats* self, void*, float amount) {
    SameBlocks<int>("UnitStats::TakePendingExperience", StatsBlock(self), kNone,
                    [&] { self->TakePendingExperience(amount); return 0; },
                    [&] { s_takePending(self, nullptr, amount); return 0; });
}
EI_VERIFY(0x00523C20, TakePendingVerify, 6, s_takePending);

// 005239B0: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes). Writes the stats and the vitals of the
// unit's server object (when it is one).
static void(__fastcall* s_gainExperience)(UnitStats*, void*, CUnit*, float);
static void __fastcall GainExperienceVerify(UnitStats* self, void*, CUnit* unit, float amount) {
    SameBlocks<int>("UnitStats::GainExperience", StatsBlock(self), ServerVitalsBlock(unit),
                    [&] { self->GainExperience(unit, amount); return 0; },
                    [&] { s_gainExperience(self, nullptr, unit, amount); return 0; });
}
EI_VERIFY(0x005239B0, GainExperienceVerify, 6, s_gainExperience);

// --- Pure ------------------------------------------------------------------------------------

// 00524060 / 00524110 / 005241C0: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes).
static float(__fastcall* s_legsFactor)(UnitStats*, void*, int);
static float __fastcall LegsFactorVerify(UnitStats* self, void*, int unused) {
    float expected = s_legsFactor(self, nullptr, unused);
    ei::verify::Check("UnitStats::LegsFactor", Bits(expected), Bits(self->LegsFactor(unused)), "%g", expected);
    return expected;
}
EI_VERIFY(0x00524060, LegsFactorVerify, 6, s_legsFactor);

static float(__fastcall* s_armsFactor)(UnitStats*, void*, int);
static float __fastcall ArmsFactorVerify(UnitStats* self, void*, int unused) {
    float expected = s_armsFactor(self, nullptr, unused);
    ei::verify::Check("UnitStats::ArmsFactor", Bits(expected), Bits(self->ArmsFactor(unused)), "%g", expected);
    return expected;
}
EI_VERIFY(0x00524110, ArmsFactorVerify, 6, s_armsFactor);

static int(__fastcall* s_movementState)(UnitStats*, void*, char);
static int __fastcall MovementStateVerify(UnitStats* self, void*, char checkLoad) {
    int expected = s_movementState(self, nullptr, checkLoad);
    ei::verify::Check("UnitStats::MovementState", expected, self->MovementState(checkLoad), "load check %d",
                      checkLoad);
    return expected;
}
EI_VERIFY(0x005241C0, MovementStateVerify, 6, s_movementState);

// 00523E80 / 00523F50: SUB ESP,8; FLD [global] (9 bytes). 00524320: SUB ESP,0xC; FLD [ECX+0x18]
// (6 bytes).
static double(__fastcall* s_hitPointCurve)(UnitStats*);
static double __fastcall HitPointCurveVerify(UnitStats* self) {
    double expected = s_hitPointCurve(self);
    double actual = self->HitPointCurve();
    ei::verify::Check("UnitStats::HitPointCurve", Bits(expected), Bits(actual), "%g vs %g", expected, actual);
    return expected;
}
EI_VERIFY(0x00523E80, HitPointCurveVerify, 9, s_hitPointCurve);

static double(__fastcall* s_manaCurve)(UnitStats*);
static double __fastcall ManaCurveVerify(UnitStats* self) {
    double expected = s_manaCurve(self);
    double actual = self->ManaCurve();
    ei::verify::Check("UnitStats::ManaCurve", Bits(expected), Bits(actual), "%g vs %g", expected, actual);
    return expected;
}
EI_VERIFY(0x00523F50, ManaCurveVerify, 9, s_manaCurve);

static double(__fastcall* s_rateValue)(UnitStats*);
static double __fastcall RateValueVerify(UnitStats* self) {
    double expected = s_rateValue(self);
    double actual = self->RateValue();
    ei::verify::Check("UnitStats::RateValue", Bits(expected), Bits(actual), "%g vs %g", expected, actual);
    return expected;
}
EI_VERIFY(0x00524320, RateValueVerify, 6, s_rateValue);

// 00523D30 / 00523E00: PUSH EBP; MOV EBP,ESP; PUSH ECX; PUSH ESI (5 bytes). 00523DB0: PUSH ESI;
// MOV ESI,[ESP+8] (5 bytes).
static float(__stdcall* s_scaleDamage)(CUnit*, float);
static float __stdcall ScaleDamageVerify(CUnit* unit, float value) {
    float expected = s_scaleDamage(unit, value);
    float actual = ScaleDamageByEffects(unit, value);
    ei::verify::Check("ScaleDamageByEffects", Bits(expected), Bits(actual), "%g: %g vs %g", value, expected, actual);
    return expected;
}
EI_VERIFY(0x00523D30, ScaleDamageVerify, 5, s_scaleDamage);

static double(__stdcall* s_hitPointEffectScale)(CUnit*);
static double __stdcall HitPointEffectScaleVerify(CUnit* unit) {
    double expected = s_hitPointEffectScale(unit);
    double actual = HitPointEffectScale(unit);
    ei::verify::Check("HitPointEffectScale", Bits(expected), Bits(actual), "%g vs %g", expected, actual);
    return expected;
}
EI_VERIFY(0x00523DB0, HitPointEffectScaleVerify, 5, s_hitPointEffectScale);

static float(__stdcall* s_scaleHitPoints)(CUnit*, float);
static float __stdcall ScaleHitPointsVerify(CUnit* unit, float value) {
    float expected = s_scaleHitPoints(unit, value);
    float actual = ScaleHitPointsByEffects(unit, value);
    ei::verify::Check("ScaleHitPointsByEffects", Bits(expected), Bits(actual), "%g: %g vs %g", value, expected,
                      actual);
    return expected;
}
EI_VERIFY(0x00523E00, ScaleHitPointsVerify, 5, s_scaleHitPoints);

// --- Packing and resets (output only) --------------------------------------------------------

// Runs the original into a scratch copy of `size` bytes at `out` and ours into `out`; compares
// the bytes and the results.
template <typename R, typename Ours, typename Original>
R SameOutput(const char* name, void* out, int size, Ours ours, Original original) {
    unsigned char scratch[0x710];
    std::memcpy(scratch, out, size);
    R expected = original(scratch);
    R actual = ours();
    int diff = -1;
    for (int i = 0; i < size && diff < 0; ++i)
        if (scratch[i] != static_cast<unsigned char*>(out)[i]) diff = i;
    ei::verify::Check(name, true, diff < 0 && std::memcmp(&expected, &actual, sizeof(R)) == 0,
                      "first difference at +0x%X", diff);
    return actual;
}

std::uintptr_t Offset(void* result, void* base) {
    return reinterpret_cast<std::uintptr_t>(result) - reinterpret_cast<std::uintptr_t>(base);
}

// 00522C50: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes). Returns the end of the output.
static void*(__fastcall* s_packVitals)(UnitVitals*, void*, UnitVitalsPacked*);
static void* __fastcall PackVitalsVerify(UnitVitals* self, void*, UnitVitalsPacked* out) {
    std::uintptr_t expected = 0;
    std::uintptr_t actual = 0;
    void* result = nullptr;
    unsigned char scratch[sizeof(UnitVitalsPacked)];
    std::memcpy(scratch, out, sizeof(scratch));
    expected = Offset(s_packVitals(self, nullptr, reinterpret_cast<UnitVitalsPacked*>(scratch)), scratch);
    result = self->Pack(out);
    actual = Offset(result, out);
    bool same = std::memcmp(scratch, out, sizeof(scratch)) == 0 && expected == actual;
    ei::verify::Check("UnitVitals::Pack", true, same, "hp %g/%g", self->hitPoints, self->maxHitPoints);
    return result;
}
EI_VERIFY(0x00522C50, PackVitalsVerify, 6, s_packVitals);

// 00522CE0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x28 (6 bytes).
static void(__fastcall* s_packStats)(UnitStats*, void*, CUnit*, UnitParams*);
static void __fastcall PackStatsVerify(UnitStats* self, void*, CUnit* unused, UnitParams* params) {
    SameOutput<int>("UnitStats::Pack", params, static_cast<int>(sizeof(UnitParams)),
                    [&] { self->Pack(unused, params); return 0; },
                    [&](void* scratch) { s_packStats(self, nullptr, unused, static_cast<UnitParams*>(scratch)); return 0; });
}
EI_VERIFY(0x00522CE0, PackStatsVerify, 6, s_packStats);

// 00523370: LEA EDX,[ECX+8]; XOR EAX,EAX (5 bytes). Returns the end of the block.
static void*(__fastcall* s_resetVitals)(UnitVitals*);
static void* __fastcall ResetVitalsVerify(UnitVitals* self) {
    unsigned char scratch[sizeof(UnitVitals)];
    std::memcpy(scratch, self, sizeof(scratch));
    std::uintptr_t expected = Offset(s_resetVitals(reinterpret_cast<UnitVitals*>(scratch)), scratch);
    void* result = self->Reset();
    bool same = std::memcmp(scratch, self, sizeof(scratch)) == 0 && expected == Offset(result, self);
    ei::verify::Check("UnitVitals::Reset", true, same, "");
    return result;
}
EI_VERIFY(0x00523370, ResetVitalsVerify, 5, s_resetVitals);

// 005233B0: XOR EAX,EAX; MOV [ECX],AL; MOV [ECX+4],AX (8 bytes).
static int(__fastcall* s_resetPacked)(UnitVitalsPacked*);
static int __fastcall ResetPackedVerify(UnitVitalsPacked* self) {
    return SameOutput<int>("UnitVitalsPacked::Reset", self, static_cast<int>(sizeof(UnitVitalsPacked)),
                           [&] { return self->Reset(); },
                           [&](void* scratch) { return s_resetPacked(static_cast<UnitVitalsPacked*>(scratch)); });
}
EI_VERIFY(0x005233B0, ResetPackedVerify, 8, s_resetPacked);

// 00523550: PUSH EBX; PUSH EBP; MOV EDX,ECX; PUSH EDI (5 bytes).
static int(__fastcall* s_resetParams)(UnitParams*);
static int __fastcall ResetParamsVerify(UnitParams* self) {
    return SameOutput<int>("UnitParams::Reset", self, static_cast<int>(sizeof(UnitParams)),
                           [&] { return self->Reset(); },
                           [&](void* scratch) { return s_resetParams(static_cast<UnitParams*>(scratch)); });
}
EI_VERIFY(0x00523550, ResetParamsVerify, 5, s_resetParams);

// 005236A0 / 00523600 / 005233E0: PUSH EBP; MOV EBP,ESP; SUB ESP,n (6 bytes).
static int(__fastcall* s_resetLayer)(UnitStatLayer*);
static int __fastcall ResetLayerVerify(UnitStatLayer* self) {
    return SameOutput<int>("UnitStatLayer::Reset", self, static_cast<int>(sizeof(UnitStatLayer)),
                           [&] { return self->Reset(); },
                           [&](void* scratch) { return s_resetLayer(static_cast<UnitStatLayer*>(scratch)); });
}
EI_VERIFY(0x005236A0, ResetLayerVerify, 6, s_resetLayer);

static int(__fastcall* s_resetSlot)(UnitStatSlot*);
static int __fastcall ResetSlotVerify(UnitStatSlot* self) {
    return SameOutput<int>("UnitStatSlot::Reset", self, static_cast<int>(sizeof(UnitStatSlot)),
                           [&] { return self->Reset(); },
                           [&](void* scratch) { return s_resetSlot(static_cast<UnitStatSlot*>(scratch)); });
}
EI_VERIFY(0x00523600, ResetSlotVerify, 6, s_resetSlot);

static int(__fastcall* s_resetStats)(UnitStats*);
static int __fastcall ResetStatsVerify(UnitStats* self) {
    return SameOutput<int>("UnitStats::Reset", self, static_cast<int>(sizeof(UnitStats)),
                           [&] { return self->Reset(); },
                           [&](void* scratch) { return s_resetStats(static_cast<UnitStats*>(scratch)); });
}
EI_VERIFY(0x005233E0, ResetStatsVerify, 6, s_resetStats);

// 005232B0: PUSH EBX; PUSH ESI; PUSH EDI; LEA EDX,[ECX+0x1AC] (9 bytes).
static int(__fastcall* s_clearOuterLayers)(UnitStats*);
static int __fastcall ClearOuterLayersVerify(UnitStats* self) {
    return SameOutput<int>("UnitStats::ClearOuterLayers", self, static_cast<int>(sizeof(UnitStats)),
                           [&] { return self->ClearOuterLayers(); },
                           [&](void* scratch) { return s_clearOuterLayers(static_cast<UnitStats*>(scratch)); });
}
EI_VERIFY(0x005232B0, ClearOuterLayersVerify, 9, s_clearOuterLayers);

// --- Raising skills and base values (stateful: stats, the server object's vitals, the unit) ---

namespace {
Block UnitBlock(CUnit* unit) { return {unit, static_cast<int>(sizeof(CUnit))}; }
} // namespace

// 00528AB0 / 00528DA0: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes). AL only.
static bool(__fastcall* s_raiseSkill)(UnitStats*, void*, int, int, CUnit*);
static bool __fastcall RaiseSkillVerify(UnitStats* self, void*, int skill, int amount, CUnit* unit) {
    return SameBlocks3<bool>("UnitStats::RaiseSkill", StatsBlock(self), ServerVitalsBlock(unit), UnitBlock(unit),
                             [&] { return self->RaiseSkill(skill, amount, unit); },
                             [&] { return s_raiseSkill(self, nullptr, skill, amount, unit); });
}
EI_VERIFY(0x00528AB0, RaiseSkillVerify, 6, s_raiseSkill);

static bool(__fastcall* s_raiseAttribute)(UnitStats*, void*, int, int, CUnit*);
static bool __fastcall RaiseAttributeVerify(UnitStats* self, void*, int index, int amount, CUnit* unit) {
    return SameBlocks3<bool>("UnitStats::RaiseAttribute", StatsBlock(self), ServerVitalsBlock(unit), UnitBlock(unit),
                             [&] { return self->RaiseAttribute(index, amount, unit); },
                             [&] { return s_raiseAttribute(self, nullptr, index, amount, unit); });
}
EI_VERIFY(0x00528DA0, RaiseAttributeVerify, 6, s_raiseAttribute);

// 00529090: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes). 00529220: PUSH EBP; MOV EBP,ESP; PUSH ECX;
// PUSH EBX (5 bytes). 00529400: PUSH EBP; MOV EBP,ESP; SUB ESP,0x14 (6 bytes).
static bool(__fastcall* s_raiseStrength)(UnitStats*, void*, int, CUnit*);
static bool __fastcall RaiseStrengthVerify(UnitStats* self, void*, int amount, CUnit* unit) {
    return SameBlocks3<bool>("UnitStats::RaiseStrength", StatsBlock(self), ServerVitalsBlock(unit), UnitBlock(unit),
                             [&] { return self->RaiseStrength(amount, unit); },
                             [&] { return s_raiseStrength(self, nullptr, amount, unit); });
}
EI_VERIFY(0x00529090, RaiseStrengthVerify, 6, s_raiseStrength);

static bool(__fastcall* s_raiseDexterity)(UnitStats*, void*, int, CUnit*);
static bool __fastcall RaiseDexterityVerify(UnitStats* self, void*, int amount, CUnit* unit) {
    return SameBlocks3<bool>("UnitStats::RaiseDexterity", StatsBlock(self), ServerVitalsBlock(unit), UnitBlock(unit),
                             [&] { return self->RaiseDexterity(amount, unit); },
                             [&] { return s_raiseDexterity(self, nullptr, amount, unit); });
}
EI_VERIFY(0x00529220, RaiseDexterityVerify, 5, s_raiseDexterity);

static bool(__fastcall* s_raiseIntelligence)(UnitStats*, void*, int, CUnit*);
static bool __fastcall RaiseIntelligenceVerify(UnitStats* self, void*, int amount, CUnit* unit) {
    return SameBlocks3<bool>("UnitStats::RaiseIntelligence", StatsBlock(self), ServerVitalsBlock(unit),
                             UnitBlock(unit), [&] { return self->RaiseIntelligence(amount, unit); },
                             [&] { return s_raiseIntelligence(self, nullptr, amount, unit); });
}
EI_VERIFY(0x00529400, RaiseIntelligenceVerify, 6, s_raiseIntelligence);

// 005282D0: pure (exact, compared as double). MOV EDX,[ESP+4]; SUB ESP,8 (7 bytes).
static double(__fastcall* s_levelValueCopy)(UnitStats*, void*, int);
static double __fastcall LevelValueCopyVerify(UnitStats* self, void*, int skill) {
    double expected = s_levelValueCopy(self, nullptr, skill);
    double actual = self->SkillLevelValueCopy(skill);
    ei::verify::Check("UnitStats::SkillLevelValueCopy", Bits(expected), Bits(actual), "skill %d", skill);
    return expected;
}
EI_VERIFY(0x005282D0, LevelValueCopyVerify, 7, s_levelValueCopy);
