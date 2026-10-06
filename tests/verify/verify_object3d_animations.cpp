// Checks for object3d_animations.cpp (A-U4).
// - RandomAnimation (005190C0) / RandomAnimationOfKind (00519380): the random generators are saved,
//   the original runs, they are put back and ours runs; the entry and the generator state after
//   must match (the database is loaded by whichever runs first, the same either way).
// - The candidate list helpers 0051A010 (SetAtGrow) / 0051A190 (SetSize): ours only; size, value.
// Reached by: CUnit::FindAnimation (005301B0) for every unit's idle, attack and death animations
// (units, combat, unit_ai, movement).
#include "verify.h"

#include "combat_verify_util.h"

#include "../../src/game/map_object_figures.h"

#include <cstdint>

namespace {
void* s_unusedSetAtGrow;
void* s_unusedSetSize;

std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
} // namespace

// 005190C0 / 00519380: MOV EAX,FS:[0] (6 bytes).
static FigureAnimation*(__fastcall* s_randomAnimation)(CObject3D*, void*, std::uint32_t);
static FigureAnimation* __fastcall RandomAnimationVerify(CObject3D* self, void*, std::uint32_t key) {
    combatverify::RandomState before, afterOriginal, afterOurs;
    before.Save();
    FigureAnimation* expected = s_randomAnimation(self, nullptr, key);
    afterOriginal.Save();
    before.Restore();
    FigureAnimation* actual = self->RandomAnimation(key);
    afterOurs.Save();
    ei::verify::Check("CObject3D::RandomAnimation", Address(expected), Address(actual), "key 0x%08X", key);
    ei::verify::Check("CObject3D::RandomAnimation random", true, afterOriginal.SameAs(afterOurs), "key 0x%08X", key);
    return actual;
}
EI_VERIFY(0x005190C0, RandomAnimationVerify, 6, s_randomAnimation);

static FigureAnimation*(__fastcall* s_randomAnimationOfKind)(CObject3D*, void*, std::uint32_t);
static FigureAnimation* __fastcall RandomAnimationOfKindVerify(CObject3D* self, void*, std::uint32_t key) {
    combatverify::RandomState before, afterOriginal, afterOurs;
    before.Save();
    FigureAnimation* expected = s_randomAnimationOfKind(self, nullptr, key);
    afterOriginal.Save();
    before.Restore();
    FigureAnimation* actual = self->RandomAnimationOfKind(key);
    afterOurs.Save();
    ei::verify::Check("CObject3D::RandomAnimationOfKind", Address(expected), Address(actual), "key 0x%08X", key);
    ei::verify::Check("CObject3D::RandomAnimationOfKind random", true, afterOriginal.SameAs(afterOurs), "key 0x%08X",
                      key);
    return actual;
}
EI_VERIFY(0x00519380, RandomAnimationOfKindVerify, 6, s_randomAnimationOfKind);

static void __fastcall CandidatesSetAtGrowVerify(AlignedPtrArray* self, void*, int index, void* value) {
    int size = self->size;
    self->SetAtGrow(index, value);
    bool ok = self->data[index] == value && self->size == (index >= size ? index + 1 : size);
    ei::verify::Check("AlignedPtrArray::SetAtGrow (0051A010)", true, ok, "index %d size %d -> %d", index, size,
                      self->size);
}
EI_VERIFY(0x0051A010, CandidatesSetAtGrowVerify, 0, s_unusedSetAtGrow);

static void __fastcall CandidatesSetSizeVerify(AlignedPtrArray* self, void*, int size, int growBy) {
    self->SetSize(size, growBy);
    bool ok = self->size == size && self->capacity >= size && (size == 0) == (self->data == nullptr);
    ei::verify::Check("AlignedPtrArray::SetSize (0051A190)", true, ok, "size %d capacity %d", self->size,
                      self->capacity);
}
EI_VERIFY(0x0051A190, CandidatesSetSizeVerify, 0, s_unusedSetSize);
