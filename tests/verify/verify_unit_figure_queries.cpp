// Checks for unit_figure_queries.cpp (U11). All five compare against the original:
// - FindAnimation (005301B0) picks entries with rand() (005190C0 / 00519380): the generators are
//   saved, the original runs, the generators are put back and ours runs; the entry and the
//   generator state afterwards must match.
// - AnimationEntry (00530240) and BodyExtent (005304A0) are pure (AnimationEntry's message goes to
//   the DebugLog stub, 0044C470 is a bare RET): both run; the entry bitwise, the extent CheckNear.
// - EffectPoint (00532860) writes only `out`: the original into a scratch vector, ours into `out`;
//   the three coordinates CheckNear. Not compared where the original turns uninitialised stack (a
//   server world, a hero without the part "rh3"); those calls only count as matches.
// - GroundHeight (00530280) starts PUSH ESI; PUSH EDI; MOV ESI,ECX; CALL, too short for a 5-byte
//   trampoline: a copy of its whole body (98 bytes, only short internal jumps), taken when this
//   DLL loads, with the CALL's displacement moved for the copy, runs as the original. CheckNear.
// Reached by: 005301B0 / 00530240 every unit's animation queue and orders (combat_motion.cpp,
// combat_steps.cpp, unit_motion_tick.cpp: units, combat, unit_ai, movement); 005304A0 CUnitServer
// slot 23 (00552FB0, every unit on the server: units, combat, unit_create); 00530280 sight and
// planning (combat_sight.cpp, combat_planning.cpp, combat_steps.cpp), client path placement
// (00535850) and the script's GetZ-like expressions (combat, units, ui_order_move); 00532860 the
// client's lightning and particle effects at a unit (spells_cast, effects, magic_traps).
#include "verify.h"

#include "combat_verify_util.h"

#include "../../src/game/originals_units.h"
#include "../../src/game/unit.h"
#include "../../src/game/world.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <windows.h>

namespace {
std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
} // namespace

// 005301B0: PUSH EDI; MOV EDI,ECX; MOV EAX,[EDI+0x10] (6 bytes).
static char*(__fastcall* s_findAnimation)(CUnit*, void*, std::uint32_t, bool);
static char* __fastcall FindAnimationVerify(CUnit* self, void*, std::uint32_t key, bool anyOfKind) {
    combatverify::RandomState before;
    combatverify::RandomState afterOriginal;
    combatverify::RandomState afterOurs;
    before.Save();
    char* expected = s_findAnimation(self, nullptr, key, anyOfKind);
    afterOriginal.Save();
    before.Restore();
    char* actual = self->FindAnimation(key, anyOfKind);
    afterOurs.Save();
    ei::verify::Check("CUnit::FindAnimation", Address(expected), Address(actual), "unit %d type 0x%X key 0x%08X any %d",
                      self->id_, self->type_, key, anyOfKind ? 1 : 0);
    ei::verify::Check("CUnit::FindAnimation random", true, afterOriginal.SameAs(afterOurs), "unit %d key 0x%08X",
                      self->id_, key);
    return actual;
}
EI_VERIFY(0x005301B0, FindAnimationVerify, 6, s_findAnimation);

// 00530240: PUSH EBX; PUSH ESI; MOV ESI,[ESP+0xC] (6 bytes).
static char*(__fastcall* s_animationEntry)(CUnit*, void*, std::uint32_t);
static char* __fastcall AnimationEntryVerify(CUnit* self, void*, std::uint32_t key) {
    char* expected = s_animationEntry(self, nullptr, key);
    char* actual = self->AnimationEntry(key);
    ei::verify::Check("CUnit::AnimationEntry", Address(expected), Address(actual), "unit %d key 0x%08X", self->id_, key);
    return actual;
}
EI_VERIFY(0x00530240, AnimationEntryVerify, 6, s_animationEntry);

// 005304A0: PUSH EBP; PUSH EDI; MOV EBP,ECX; PUSH 0x0078AD38 (9 bytes).
static double(__fastcall* s_bodyExtent)(CUnit*);
static double __fastcall BodyExtentVerify(CUnit* self) {
    double expected = s_bodyExtent(self);
    double actual = self->BodyExtent();
    ei::verify::CheckNear("CUnit::BodyExtent", expected, actual, "unit %d type 0x%X '%s': %g vs %g", self->id_,
                          self->type_, self->prototypeName ? self->prototypeName : "", expected, actual);
    return actual;
}
EI_VERIFY(0x005304A0, BodyExtentVerify, 9, s_bodyExtent);

// 00532860: SUB ESP,0x64; PUSH ESI; MOV ESI,ECX (6 bytes).
static Vector3*(__fastcall* s_effectPoint)(CUnit*, void*, Vector3*);
static Vector3* __fastcall EffectPointVerify(CUnit* self, void*, Vector3* out) {
    Vector3 expected = *out;
    s_effectPoint(self, nullptr, &expected);
    self->EffectPoint(out);
    auto* world = static_cast<CWorld*>(self->ManagerContext());
    bool defined = !world->IsServer() &&
                   !(self->type_ == kUnitTypeHero && !ObjectFindPartOriginal(self, nullptr, "rh3"));
    if (!defined) {
        ei::verify::Check("CUnit::EffectPoint", 0, 0, "");
        return out;
    }
    const float* want = &expected.x;
    const float* got = &out->x;
    for (int i = 0; i < 3; ++i) {
        ei::verify::CheckNear("CUnit::EffectPoint", want[i], got[i], "unit %d type 0x%X [%d]: %g vs %g", self->id_,
                              self->type_, i, want[i], got[i]);
    }
    return out;
}
EI_VERIFY(0x00532860, EffectPointVerify, 6, s_effectPoint);

namespace {
// A copy of 00530280 that runs from elsewhere: its only relative operand outside the body is the
// CALL 00455950 at +4, whose displacement is recomputed for the copy. Null when the bytes are not
// the expected ones (another patch).
using GroundHeightFn = double(__fastcall*)(CUnit*, void*, float, float);
constexpr std::uintptr_t kGroundHeight = 0x00530280;
constexpr std::uintptr_t kObjectWorld = 0x00455950;
constexpr std::size_t kGroundHeightSize = 98;   // through RET 8 at 005302DF
constexpr std::size_t kCallAt = 4;

GroundHeightFn CopyGroundHeight() {
    const auto* source = reinterpret_cast<const std::uint8_t*>(kGroundHeight);
    std::int32_t displacement;
    std::memcpy(&displacement, source + kCallAt + 1, sizeof(displacement));
    if (source[kCallAt] != 0xE8 || kGroundHeight + kCallAt + 5 + displacement != kObjectWorld) return nullptr;
    auto* copy = static_cast<std::uint8_t*>(
        ::VirtualAlloc(nullptr, kGroundHeightSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!copy) return nullptr;
    std::memcpy(copy, source, kGroundHeightSize);
    displacement = static_cast<std::int32_t>(kObjectWorld - (reinterpret_cast<std::uintptr_t>(copy) + kCallAt + 5));
    std::memcpy(copy + kCallAt + 1, &displacement, sizeof(displacement));
    return reinterpret_cast<GroundHeightFn>(copy);
}
GroundHeightFn s_groundHeightCopy = CopyGroundHeight();
void* s_groundHeightUnused;
} // namespace

static double __fastcall GroundHeightVerify(CUnit* self, void*, float x, float y) {
    double actual = self->GroundHeight(x, y);
    if (s_groundHeightCopy) {
        double expected = s_groundHeightCopy(self, nullptr, x, y);
        ei::verify::CheckNear("CUnit::GroundHeight", expected, actual, "unit %d kind %d at (%g, %g): %g vs %g",
                              self->id_, self->movementKind, x, y, expected, actual);
    }
    return actual;
}
EI_VERIFY(0x00530280, GroundHeightVerify, 0, s_groundHeightUnused);
