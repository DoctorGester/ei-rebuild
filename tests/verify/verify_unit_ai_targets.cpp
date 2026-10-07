// Checks for the target choice (src/game/unit_ai_targets.cpp). Reached by the guard / patrol /
// sentry modes' LookAround and the aggression mode (tests/scenarios/unit_ai.txt) and by party units
// in combat (PlayerMode's combat step 005D4170: combat.txt, combat_more.txt).
#include "verify.h"

#include "../../src/game/unit_ai.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;
}

// 005CFE20 AIDistance2: FLD [ECX+4]; FSUB [EDX+4] (6 bytes). Pure, within a few ULPs.
static double(__fastcall* s_distance2)(const Vector3*, const Vector3*);
static double __fastcall Distance2Verify(const Vector3* a, const Vector3* b) {
    double expected = s_distance2(a, b);
    double actual = AIDistance2(a, b);
    ei::verify::CheckNear("AIDistance2", expected, actual, "(%f, %f) - (%f, %f)", a->x, a->y, b->x, b->y);
    return expected;
}
EI_VERIFY(0x005CFE20, Distance2Verify, 6, s_distance2);

// 00545F40 AIOrder::Clear: releases the object, so ours only: kind 9, no object, flag 0.
static void __fastcall ClearOrderVerify(AIOrder* order) {
    order->Clear();
    ei::verify::Check("AIOrder::Clear", true, order->kind == 9 && !order->object && order->flag == 0, "order %p", order);
}
EI_VERIFY(0x00545F40, ClearOrderVerify, 0, s_unused);

// 005CF720 / 005CF780 MakeAttack / MakeCast: ours only: kind, what and object as asked, flag 0.
static void __fastcall MakeAttackVerify(AIOrder* order, void*, CEngineObject* target) {
    order->MakeAttack(target);
    ei::verify::Check("AIOrder::MakeAttack", true, order->kind == 3 && order->what == 6 && order->object == target && order->flag == 0,
                      "order %p target %p", order, target);
}
EI_VERIFY(0x005CF720, MakeAttackVerify, 0, s_unused);

static void __fastcall MakeCastVerify(AIOrder* order, void*, int spell, CEngineObject* target) {
    order->MakeCast(spell, target);
    ei::verify::Check("AIOrder::MakeCast", true, order->kind == 5 && order->what == spell && order->object == target && order->flag == 0,
                      "order %p spell %d target %p", order, spell, target);
}
EI_VERIFY(0x005CF780, MakeCastVerify, 0, s_unused);

// 005CF7F0 IsUsable: MOV EAX,[ESP+8]; SUB ESP,0x10 (7 bytes). Pure (AL compared).
static bool(__fastcall* s_isUsable)(AIAttention*, void*, AIBrain*, const int*);
static bool __fastcall IsUsableVerify(AIAttention* self, void*, AIBrain* brain, const int* partAndObject) {
    bool expected = s_isUsable(self, nullptr, brain, partAndObject);
    bool actual = self->IsUsable(brain, partAndObject);
    ei::verify::Check("AIAttention::IsUsable", expected, actual, "brain %p part %d object %08X", brain, partAndObject[0],
                      partAndObject[1]);
    return expected;
}
EI_VERIFY(0x005CF7F0, IsUsableVerify, 7, s_isUsable);

// 005CF9A0 Score: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes). Pure: the ST0 result within a few
// ULPs. A sum or comparison on a rounding edge could take another branch (rare).
static double(__fastcall* s_score)(AIAttention*, void*, const Vector3*, CUnit*, AIObjectList*, CEngineObject*, CUnit*, float,
                                   const AIAttackOption*);
static double __fastcall ScoreVerify(AIAttention* self, void*, const Vector3* from, CUnit* unit, AIObjectList* group,
                                     CEngineObject* current, CUnit* candidate, float threshold, const AIAttackOption* option) {
    double expected = s_score(self, nullptr, from, unit, group, current, candidate, threshold, option);
    double actual = self->Score(from, unit, group, current, candidate, threshold, option);
    ei::verify::CheckNear("AIAttention::Score", expected, actual, "kind %d part %d candidate %p threshold %f: %f vs %f", option->kind,
                          option->part, candidate, threshold, expected, actual);
    return expected;
}
EI_VERIFY(0x005CF9A0, ScoreVerify, 6, s_score);

// 005D0030 Choose: MOV EAX,FS:[0] (6 bytes). Differential: the original first; the only state it
// leaves behind is reports it added (a reference each) or raised, which are put back before ours
// runs. The chosen pair and the report array afterwards are compared. Both run the reach test
// (a path search, no lasting state).
static void(__fastcall* s_choose)(AIAttention*, void*, AIBrain*, int*, AIObjectList*, const Vector3*, bool, int);
static void __fastcall ChooseVerify(AIAttention* self, void*, AIBrain* brain, int* partAndObject, AIObjectList* candidates,
                                    const Vector3* from, bool alliesOnly, int reachArg) {
    AIReportArray& reports = brain->reports;
    int size = reports.size;
    std::vector<AIReport> before(reports.data, reports.data + size);
    int theirs[2] = {partAndObject[0], partAndObject[1]};
    s_choose(self, nullptr, brain, theirs, candidates, from, alliesOnly, reachArg);
    std::vector<AIReport> afterTheirs(reports.data, reports.data + reports.size);
    for (int i = size; i < reports.size; ++i) {
        --reports.data[i].object->refCount_;
        reports.data[i].object = nullptr;
    }
    reports.size = size;
    for (int i = 0; i < size; ++i) reports.data[i].what = before[i].what;
    self->Choose(brain, partAndObject, candidates, from, alliesOnly, reachArg);
    bool same = theirs[0] == partAndObject[0] && theirs[1] == partAndObject[1] && static_cast<int>(afterTheirs.size()) == reports.size &&
                (reports.size == 0 || std::memcmp(afterTheirs.data(), reports.data, sizeof(AIReport) * reports.size) == 0);
    ei::verify::Check("AIAttention::Choose", true, same, "brain %p: {%d, %08X} vs {%d, %08X}, reports %d vs %d", brain, theirs[0],
                      theirs[1], partAndObject[0], partAndObject[1], static_cast<int>(afterTheirs.size()), reports.size);
}
EI_VERIFY(0x005D0030, ChooseVerify, 6, s_choose);

// 005CF3C0 CanReach: PUSH -1; PUSH 0x728068 (7 bytes). Differential: the original, then ours; AL
// compared. Both run the path search and lift the target off the sector map and back (no lasting
// state but the target's place in its sector list). Reached through Choose (above) for the target
// it picks: unit_ai (aggression), combat.
static bool(__fastcall* s_canReach)(AIAttention*, void*, AIBrain*, const int*, int);
static bool __fastcall CanReachVerify(AIAttention* self, void*, AIBrain* brain, const int* partAndObject, int fromHere) {
    bool expected = s_canReach(self, nullptr, brain, partAndObject, fromHere);
    bool actual = self->CanReach(brain, partAndObject, fromHere);
    ei::verify::Check("AIAttention::CanReach", expected, actual, "brain %p part %d object %08X here %d", brain, partAndObject[0],
                      partAndObject[1], fromHere & 0xFF);
    return expected;
}
EI_VERIFY(0x005CF3C0, CanReachVerify, 7, s_canReach);
