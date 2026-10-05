// Checks for the brain's attention (src/game/unit_ai_attention.cpp) and the memory's per-tick update
// and IsEnemy (src/game/unit_ai_brain.cpp).
#include "verify.h"

#include "../../src/game/unit_ai.h"

#include <cstddef>
#include <cstdint>

namespace {
void* s_unused;

AIBrain* BrainOfMemory(std::uint8_t* memory) { return reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory)); }
} // namespace

// 005CF220 CurrentAttack: MOV EAX,[ESP+4]; PUSH ESI (5 bytes). Output only: the original fills a
// scratch pair seeded like the caller's, ours the caller's; both dwords compared.
static void(__fastcall* s_currentAttack)(std::uint8_t*, void*, AIBrain*, int*);
static void __fastcall CurrentAttackVerify(std::uint8_t* attention, void*, AIBrain* brain, int* partAndObject) {
    int theirs[2] = {partAndObject[0], partAndObject[1]};
    s_currentAttack(attention, nullptr, brain, theirs);
    brain->CurrentAttack(partAndObject);
    ei::verify::Check("AIBrain::CurrentAttack part", theirs[0], partAndObject[0], "brain %p action %d", brain, brain->action);
    ei::verify::Check("AIBrain::CurrentAttack object", theirs[1], partAndObject[1], "brain %p action %d", brain,
                      brain->action);
    ei::verify::Check("AIBrain::CurrentAttack this", static_cast<void*>(brain->attention), static_cast<void*>(attention),
                      "brain %p", brain);
}
EI_VERIFY(0x005CF220, CurrentAttackVerify, 5, s_currentAttack);

// 005CFE40 LookAround: ours only (it chooses a target and orders the attack). Afterwards an attack
// or cast action has a target; `this` is the brain's +0xA4.
static void __fastcall LookAroundVerify(std::uint8_t* attention, void*, AIBrain* brain, int unused) {
    brain->LookAround(unused);
    bool ok = attention == brain->attention &&
              !((brain->action == AIBrain::kActionAttack) && brain->target == nullptr);
    ei::verify::Check("AIBrain::LookAround", true, ok, "brain %p action %d target %p", brain, brain->action, brain->target);
}
EI_VERIFY(0x005CFE40, LookAroundVerify, 0, s_unused);

// 005CB700 UpdateMemory (Think calls it through its address): ours only; no report is left below
// 0 and the noticed level grew by the noticed kind.
static void __fastcall UpdateMemoryVerify(std::uint8_t* memory, void*, UnitController* owner) {
    AIBrain* brain = BrainOfMemory(memory);
    int expectedLevel = brain->noticedLevel + brain->noticedKind;
    brain->UpdateMemory(owner);
    bool ok = brain->noticedLevel == expectedLevel;
    for (int i = 0; i < brain->reports.size; ++i) ok = ok && brain->reports.data[i].what >= 0 && brain->reports.data[i].object;
    ei::verify::Check("AIBrain::UpdateMemory", true, ok, "brain %p: %d reports", brain, brain->reports.size);
}
EI_VERIFY(0x005CB700, UpdateMemoryVerify, 0, s_unused);

// 005CB7F0 IsEnemy: MOV EAX,[ESP+4]; MOV EDX,[ESP+8] (8 bytes). Pure.
static int(__fastcall* s_isEnemy)(std::uint8_t*, void*, CUnit*, CEngineObject*);
static int __fastcall IsEnemyVerify(std::uint8_t* memory, void*, CUnit* self, CEngineObject* other) {
    int expected = s_isEnemy(memory, nullptr, self, other);
    ei::verify::Check("AIBrain::IsEnemy", expected, BrainOfMemory(memory)->IsEnemy(self, other), "unit %p other %p", self,
                      other);
    return expected;
}
EI_VERIFY(0x005CB7F0, IsEnemyVerify, 8, s_isEnemy);
