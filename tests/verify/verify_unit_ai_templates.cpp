// Checks for the abstract mode bases' constructors, the patrol point array's destructors and two
// template helpers (src/game/unit_ai_mode_lifetime.cpp). Scenarios: unit_ai.txt (modes built by
// UM* commands and cleared by UMClear; party units' player modes), script_exec.txt.
#include "verify.h"

#include "../../src/game/unit_ai.h"

namespace {
void* s_unused;
}

// 005D8850 / 005D6D10 the abstract bases' constructors: MOV EAX,ECX; MOV [EAX],imm32 (8 bytes).
// Output: the original on a copy; vtable and the returned pointer compared.
static AIMode*(__fastcall* s_modeBase)(AIMode*);
static AIMode* __fastcall ModeBaseVerify(AIMode* self) {
    AIMode theirs = *self;
    bool sameResult = s_modeBase(&theirs) == &theirs;
    AIMode* actual = aimode::ConstructModeBase(self);
    ei::verify::Check("aimode::ConstructModeBase", true, sameResult && actual == self && theirs.vtable == self->vtable, "mode %p", self);
    return actual;
}
EI_VERIFY(0x005D8850, ModeBaseVerify, 8, s_modeBase);

static AIMode*(__fastcall* s_actionBase)(AIMode*);
static AIMode* __fastcall ActionBaseVerify(AIMode* self) {
    AIMode theirs = *self;
    bool sameResult = s_actionBase(&theirs) == &theirs;
    AIMode* actual = aimode::ConstructActionBase(self);
    ei::verify::Check("aimode::ConstructActionBase", true, sameResult && actual == self && theirs.vtable == self->vtable, "mode %p",
                      self);
    return actual;
}
EI_VERIFY(0x005D6D10, ActionBaseVerify, 8, s_actionBase);

// 005D8CF0 / 005D8CD0 the patrol point array's destructors: free memory, so ours only: the base
// vtable afterwards / `this` returned.
static void __fastcall PointsDestructVerify(PatrolPointArray* self) {
    self->Destruct();
    ei::verify::Check("PatrolPointArray::Destruct", aimode::kPatrolPointsBaseVtable, self->vtable, "array %p", self);
}
EI_VERIFY(0x005D8CF0, PointsDestructVerify, 0, s_unused);

static PatrolPointArray* __fastcall PointsDeleteVerify(PatrolPointArray* self, void*, int flags) {
    PatrolPointArray* result = self->DeletingDestruct(flags);
    ei::verify::Check("PatrolPointArray::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "flags %d", flags);
    return result;
}
EI_VERIFY(0x005D8CD0, PointsDeleteVerify, 0, s_unused);

// 005D9E10 ZeroAttackOptions: ours only: every byte zero.
static void __stdcall ZeroAttackOptionsVerify(AIAttackOption* first, int count) {
    ZeroAttackOptions(first, count);
    bool ok = true;
    const auto* bytes = reinterpret_cast<const unsigned char*>(first);
    for (int i = 0; ok && i < count * static_cast<int>(sizeof(AIAttackOption)); ++i) ok = bytes[i] == 0;
    ei::verify::Check("ZeroAttackOptions", true, ok, "%p x %d", first, count);
}
EI_VERIFY(0x005D9E10, ZeroAttackOptionsVerify, 0, s_unused);

// 005D9830 ListGetNext: MOV ECX,[ESP+4]; MOV EAX,[ECX] (6 bytes). The original on a copy of the
// position; the returned slot and the new position compared.
static void*(__stdcall* s_listGetNext)(void**);
static void* __stdcall ListGetNextVerify(void** position) {
    void* theirPosition = *position;
    void* expected = s_listGetNext(&theirPosition);
    void* actual = ListGetNext(position);
    ei::verify::Check("ListGetNext", true, expected == actual && theirPosition == *position, "node %p", actual);
    return actual;
}
EI_VERIFY(0x005D9830, ListGetNextVerify, 6, s_listGetNext);
