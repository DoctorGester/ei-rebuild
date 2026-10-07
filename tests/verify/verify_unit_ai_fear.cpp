// Checks for the Fear mode (src/game/unit_ai_fear.cpp): Evaluate differential, Execute and the
// brain's ApplyAttack ours only. Scenario: unit_ai_fear (UMFear on a ranged unit next to an enemy);
// every StandardGuard / StandardSentry unit (script Guard / Sentry) evaluates its Fear mode each tick.
#include "verify.h"

#include "../../src/game/random.h"
#include "../../src/game/unit_ai.h"

#include <cstring>

namespace {
void* s_unused;

// What Evaluate may change: the mode, the brain's noticed point / level / kind, the random state.
struct FearState {
    unsigned char mode[sizeof(FearMode)];
    float noticedX, noticedY;
    int noticedLevel, noticedKind;
    IsaacRandom random;

    void Take(const FearMode* fear, const AIBrain* brain) {
        std::memcpy(mode, fear, sizeof(mode));
        noticedX = brain->noticedX;
        noticedY = brain->noticedY;
        noticedLevel = brain->noticedLevel;
        noticedKind = brain->noticedKind;
        std::memcpy(&random, &IsaacRandom::Global(), sizeof(IsaacRandom));
    }
    void Put(FearMode* fear, AIBrain* brain) const {
        std::memcpy(fear, mode, sizeof(mode));
        brain->noticedX = noticedX;
        brain->noticedY = noticedY;
        brain->noticedLevel = noticedLevel;
        brain->noticedKind = noticedKind;
        std::memcpy(&IsaacRandom::Global(), &random, sizeof(IsaacRandom));
    }
    const FearMode* Mode() const { return reinterpret_cast<const FearMode*>(mode); }
};

void CheckVector(const char* name, const Vector3& a, const Vector3& b, const void* mode) {
    ei::verify::CheckNear(name, a.x, b.x, "mode %p x", mode);
    ei::verify::CheckNear(name, a.y, b.y, "mode %p y", mode);
    ei::verify::CheckNear(name, a.z, b.z, "mode %p z", mode);
}
} // namespace

// 005D2960 FearMode::Evaluate: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes). Differential: the original
// runs first and its result and state are kept, the state before is put back (the mode, the
// noticed fields, the random generator), ours runs; result, fields and random state compared.
// FindPath and the order check leave no state behind (the unit is moved back).
static int(__fastcall* s_fearEvaluate)(FearMode*, void*, AIBrain*);
static int __fastcall FearEvaluateVerify(FearMode* self, void*, AIBrain* brain) {
    static FearState before, theirs;
    before.Take(self, brain);
    int expected = s_fearEvaluate(self, nullptr, brain);
    theirs.Take(self, brain);
    before.Put(self, brain);
    int actual = self->Evaluate(brain);
    const FearMode* t = theirs.Mode();
    ei::verify::Check("FearMode::Evaluate", expected, actual, "mode %p", self);
    ei::verify::CheckNear("FearMode::Evaluate distance", t->distance, self->distance, "mode %p", self);
    ei::verify::CheckNear("FearMode::Evaluate spread", t->spread, self->spread, "mode %p", self);
    CheckVector("FearMode::Evaluate shelter", t->shelter, self->shelter, self);
    CheckVector("FearMode::Evaluate fleePoint", t->fleePoint, self->fleePoint, self);
    bool noticed = theirs.noticedX == brain->noticedX && theirs.noticedY == brain->noticedY &&
                   theirs.noticedLevel == brain->noticedLevel && theirs.noticedKind == brain->noticedKind;
    ei::verify::Check("FearMode::Evaluate noticed", true, noticed, "mode %p", self);
    bool sameRandom = std::memcmp(&theirs.random, &IsaacRandom::Global(), sizeof(IsaacRandom)) == 0;
    ei::verify::Check("FearMode::Evaluate random", true, sameRandom, "mode %p", self);
    return expected;
}
EI_VERIFY(0x005D2960, FearEvaluateVerify, 5, s_fearEvaluate);

// 005D30A0 FearMode::Execute: ours only. The action is a valid one; a stand-and-look (action 2)
// has no target and no look running yet; a pending flight point was used up.
static void __fastcall FearExecuteVerify(FearMode* self, void*, AIBrain* brain) {
    self->Execute(brain);
    bool ok = brain->action >= AIBrain::kActionNone && brain->action <= AIBrain::kActionFollow;
    if (brain->action == AIBrain::kActionMove) ok = ok && brain->target == nullptr;
    ok = ok && (self->fleePoint.z == 0.0f || self->fleePoint.z == 1.0f);
    ei::verify::Check("FearMode::Execute", true, ok, "mode %p action %d", self, brain->action);
}
EI_VERIFY(0x005D30A0, FearExecuteVerify, 0, s_unused);

// 005CF170 AIBrain::ApplyAttack: ours only. A cast that is not the current attack leaves the object
// as the target and the spell in actionTime.
static void __fastcall ApplyAttackVerify(AIAttention* attention, void*, AIBrain* brain, const int* partAndObject) {
    (void)attention;
    int now[2] = {-1, 0};
    brain->CurrentAttack(now);
    auto* object = reinterpret_cast<CEngineObject*>(partAndObject[1]);
    int part = partAndObject[0];
    bool current = partAndObject[1] == now[1] && part == now[0];
    brain->ApplyAttack(partAndObject);
    bool ok = true;
    if (object && !current && part != -1) ok = brain->target == object && brain->actionTime == part;
    ei::verify::Check("AIBrain::ApplyAttack", true, ok, "brain %p part %d", brain, part);
}
EI_VERIFY(0x005CF170, ApplyAttackVerify, 0, s_unused);
