// Checks for CUnit's virtual slots (unit_update.cpp).
//
// Most slots only pass the event on to the controller or the map object code (stateful, with
// effects that cannot be undone): ours only, with the checks that hold for any correct run.
// StandPoint writes only its output (the world's height queries are pure): the original into a
// scratch point. PlaceUnitOnGround and SetFromStats change only the unit: ours, snapshot,
// restore, original, compare.
#include "verify.h"

#include "../../src/game/unit.h"

#include <cstring>

namespace {
std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// Runs `ours`, snapshots the whole unit, restores it, runs `original`; compares the bytes.
template <typename Ours, typename Original>
void SameUnit(const char* name, CUnit* self, Ours ours, Original original) {
    unsigned char saved[sizeof(CUnit)];
    unsigned char after[sizeof(CUnit)];
    std::memcpy(saved, self, sizeof(CUnit));
    ours();
    std::memcpy(after, self, sizeof(CUnit));
    std::memcpy(self, saved, sizeof(CUnit));
    original();
    int diff = -1;
    for (int i = 0; i < static_cast<int>(sizeof(CUnit)) && diff < 0; ++i)
        if (after[i] != reinterpret_cast<unsigned char*>(self)[i]) diff = i;
    ei::verify::Check(name, true, diff < 0, "unit %d: first difference at +0x%X", self->id_, diff);
}

void* s_unused;
} // namespace

// 0052F7D0: the per-tick update. Ours only.
static int __fastcall TickVerify(CUnit* self) {
    int result = self->Tick();
    ei::verify::Check("CUnit::Tick", 1, result, "unit %d", self->id_);
    return result;
}
EI_VERIFY(0x0052F7D0, TickVerify, 0, s_unused);

// 00532A80 / 00532AB0 / 00532AA0 / 00530A50 / 0052EB30 / 00530A20: ours only (counted).
static std::uintptr_t __fastcall RecomputeUnitScaleVerify(CUnit* self) {
    std::uintptr_t result = self->CUnit::RecomputeScale();
    ei::verify::Check("CUnit::RecomputeUnitScale", true, true, "");
    return result;
}
EI_VERIFY(0x00532A80, RecomputeUnitScaleVerify, 0, s_unused);

static std::uintptr_t __fastcall EffectsChangedVerify(CUnit* self) {
    std::uintptr_t result = self->CUnit::EffectsChanged();
    ei::verify::Check("CUnit::EffectsChanged", true, true, "");
    return result;
}
EI_VERIFY(0x00532AB0, EffectsChangedVerify, 0, s_unused);

static std::uintptr_t __fastcall ResetUnitScaleVerify(CUnit* self) {
    std::uintptr_t result = self->CUnit::ApplyBaseScale();
    ei::verify::Check("CUnit::ResetUnitScale", true, true, "");
    return result;
}
EI_VERIFY(0x00532AA0, ResetUnitScaleVerify, 0, s_unused);

static std::uintptr_t __fastcall UpdateSizeClassesVerify(CUnit* self) {
    std::uintptr_t result = self->CUnit::UpdateSizeClasses();
    ei::verify::Check("CUnit::UpdateSizeClasses", true, true, "");
    return result;
}
EI_VERIFY(0x00530A50, UpdateSizeClassesVerify, 0, s_unused);

static std::uintptr_t __fastcall ScaleEffectChangedVerify(CUnit* self, void*, int value) {
    bool hadController = self->controller != nullptr;
    std::uintptr_t result = self->CUnit::ScaleEffectChanged(value);
    ei::verify::Check("CUnit::ScaleEffectChanged", true, hadController || result == 0, "");
    return result;
}
EI_VERIFY(0x0052EB30, ScaleEffectChangedVerify, 0, s_unused);

static std::uintptr_t __fastcall SetServerStatsVerify(CUnit* self, void*, const UnitStats* stats) {
    int classId = self->controller->GetClassId();
    std::uintptr_t result = self->CUnit::SetStats(stats);
    ei::verify::Check("CUnit::SetServerStats", true, classId == 0x3E || result == static_cast<std::uintptr_t>(classId),
                      "class %X", classId);
    return result;
}
EI_VERIFY(0x00530A20, SetServerStatsVerify, 0, s_unused);

// 0052E910: ours only; afterwards the figures are loaded (flags_ 0x80).
static std::uintptr_t __fastcall LoadUnitFiguresVerify(CUnit* self) {
    std::uintptr_t result = self->CUnit::LoadFigures();
    ei::verify::Check("CUnit::LoadUnitFigures", true, (self->flags_ & 0x80) != 0, "unit %d", self->id_);
    return result;
}
EI_VERIFY(0x0052E910, LoadUnitFiguresVerify, 0, s_unused);

// 0052E870: ours only; a controller-type companion is attached both ways and referenced.
static std::uintptr_t __fastcall AttachCompanionVerify(CUnit* self, void*, CEngineObject* companion) {
    bool isController = companion && companion->type_ == 0x48;
    std::uintptr_t result = self->CUnit::AttachObject(companion);
    bool ok = !isController ||
              (self->controller == companion &&
               *reinterpret_cast<CUnit**>(reinterpret_cast<char*>(companion) + 0x1C) == self &&
               result == static_cast<std::uintptr_t>(companion->refCount_));
    ei::verify::Check("CUnit::AttachCompanion", true, ok, "type %X", companion ? companion->type_ : -1);
    return result;
}
EI_VERIFY(0x0052E870, AttachCompanionVerify, 0, s_unused);

// 0052EB50: ours only; returns 1.
static int __fastcall StatsChangedVerify(CUnit* self) {
    int result = self->CUnit::StatsChanged();
    ei::verify::Check("CUnit::StatsChanged", 1, result, "unit %d", self->id_);
    return result;
}
EI_VERIFY(0x0052EB50, StatsChangedVerify, 0, s_unused);

// 005303A0: SUB ESP,0x10; PUSH EBX; PUSH EBP (5 bytes). The original into a scratch point.
static float*(__fastcall* s_standPoint)(CUnit*, void*, float*);
static float* __fastcall StandPointVerify(CUnit* self, void*, float* out) {
    float scratch[3];
    std::memcpy(scratch, out, sizeof(scratch));
    float* expected = s_standPoint(self, nullptr, scratch);
    float* actual = self->StandPoint(out);
    bool same = expected == scratch && actual == out && Bits(scratch[0]) == Bits(out[0]) &&
                Bits(scratch[1]) == Bits(out[1]) && Bits(scratch[2]) == Bits(out[2]);
    ei::verify::Check("CUnit::StandPoint", true, same, "z %g vs %g (mode %d, kind %u)", scratch[2], out[2],
                      self->moveMode, self->movementKind);
    return actual;
}
EI_VERIFY(0x005303A0, StandPointVerify, 5, s_standPoint);

// 0052F740: PUSH ECX; MOV EAX,[ESP+8] (5 bytes).
static void(__fastcall* s_placeOnGround)(CUnit*, void*, float);
static void __fastcall PlaceOnGroundVerify(CUnit* self, void*, float offset) {
    SameUnit("CUnit::PlaceUnitOnGround", self, [&] { self->CUnit::PlaceOnGround(offset); },
             [&] { s_placeOnGround(self, nullptr, offset); });
}
EI_VERIFY(0x0052F740, PlaceOnGroundVerify, 5, s_placeOnGround);

// 00532A20: PUSH ESI; MOV ESI,ECX; PUSH EDI; MOV EDI,[ESP+0xC] (8 bytes).
static void(__fastcall* s_setFromStats)(CUnit*, void*, UnitStats*);
static void __fastcall SetFromStatsVerify(CUnit* self, void*, UnitStats* stats) {
    SameUnit("CUnit::SetFromStats", self, [&] { self->SetFromStats(stats); },
             [&] { s_setFromStats(self, nullptr, stats); });
}
EI_VERIFY(0x00532A20, SetFromStatsVerify, 8, s_setFromStats);

// 00532D40: PUSH ESI; MOV ESI,ECX; MOV ECX,[ESI+0xC] (6 bytes). Pure.
static int(__fastcall* s_level)(CUnit*);
static int __fastcall LevelVerify(CUnit* self) {
    int expected = s_level(self);
    ei::verify::Check("CUnit::Level", expected, self->Level(), "unit %d", self->id_);
    return expected;
}
EI_VERIFY(0x00532D40, LevelVerify, 6, s_level);
