// Checks for the perception memory's queries (src/game/unit_ai_memory.cpp). Reached by the
// aggression mode's Evaluate (FindEnemies; tests/scenarios/unit_ai.txt), party units in combat
// (FindEnemiesNear from PlayerMode's combat step; combat.txt), the fear mode (NearestEnemy;
// script_exec.txt UMFear), a unit hit by another (ForceSee through SetSeen; combat.txt) and every
// corpse seen (AddCountedTail).
#include "verify.h"

#include "../../src/game/unit_ai.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
void* s_unused;

AIBrain* BrainOfMemory(std::uint8_t* memory) { return reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory)); }

std::vector<CEngineObject*> Elements(const AIObjectList& list) {
    std::vector<CEngineObject*> out;
    for (auto* node = list.head; node; node = node->next) out.push_back(node->data);
    return out;
}

AIObjectList ScratchLike(const AIObjectList& list) {
    AIObjectList scratch{};
    scratch.vtable = list.vtable;
    scratch.blockSize = list.blockSize;
    return scratch;
}
} // namespace

// 005CD500 AddCountedTail: ours only: one node more at the tail holding the object, one reference
// more on it.
static AIObjectList::Node* __fastcall AddCountedTailVerify(AIObjectList* list, void*, CEngineObject* object) {
    int count = list->count;
    int refs = object ? object->refCount_ : 0;
    AIObjectList::Node* node = AddCountedTail(list, nullptr, object);
    bool ok = list->count == count + 1 && list->tail == node && node->data == object && !node->next &&
              (!object || object->refCount_ == refs + 1);
    ei::verify::Check("AddCountedTail", true, ok, "list %p object %p", list, object);
    return node;
}
EI_VERIFY(0x005CD500, AddCountedTailVerify, 0, s_unused);

// 005CB260 FindEnemies: PUSH ECX/EBX/EBP/ESI; MOV ESI,[ESP+0x18] (8 bytes). Output: ours fills the
// caller's list first, then the original an empty scratch list (its nodes allocated after ours',
// SV2f); the element sequences compared.
static void(__fastcall* s_findEnemies)(std::uint8_t*, void*, CUnit*, AIObjectList*);
static void __fastcall FindEnemiesVerify(std::uint8_t* memory, void*, CUnit* self, AIObjectList* out) {
    BrainOfMemory(memory)->FindEnemies(self, out);
    AIObjectList scratch = ScratchLike(*out);
    s_findEnemies(memory, nullptr, self, &scratch);
    bool same = Elements(scratch) == Elements(*out);
    ei::verify::Check("AIBrain::FindEnemies", true, same, "unit %p: %d vs %d", self, scratch.count, out->count);
    scratch.RemoveAll();
}
EI_VERIFY(0x005CB260, FindEnemiesVerify, 8, s_findEnemies);

// 005CB480 FindEnemiesNear: PUSH ECX/EBP/ESI; MOV ESI,[ESP+0x14] (7 bytes). Output, as above. A
// distance on the radius' rounding edge could differ (rare).
static void(__fastcall* s_findEnemiesNear)(std::uint8_t*, void*, CUnit*, AIObjectList*, const Vector3*, float);
static void __fastcall FindEnemiesNearVerify(std::uint8_t* memory, void*, CUnit* self, AIObjectList* out, const Vector3* point,
                                             float radius) {
    BrainOfMemory(memory)->FindEnemiesNear(self, out, point, radius);
    AIObjectList scratch = ScratchLike(*out);
    s_findEnemiesNear(memory, nullptr, self, &scratch, point, radius);
    bool same = Elements(scratch) == Elements(*out);
    ei::verify::Check("AIBrain::FindEnemiesNear", true, same, "unit %p radius %f: %d vs %d", self, radius, scratch.count, out->count);
    scratch.RemoveAll();
}
EI_VERIFY(0x005CB480, FindEnemiesNearVerify, 7, s_findEnemiesNear);

// 005CB670 NearestEnemy: PUSH ECX/EBP/EDI; MOV EDI,[ECX+0x44] (6 bytes). Pure.
static CUnit*(__fastcall* s_nearestEnemy)(std::uint8_t*, void*, CUnit*);
static CUnit* __fastcall NearestEnemyVerify(std::uint8_t* memory, void*, CUnit* self) {
    CUnit* expected = s_nearestEnemy(memory, nullptr, self);
    CUnit* actual = BrainOfMemory(memory)->NearestEnemy(self);
    ei::verify::Check("AIBrain::NearestEnemy", expected, actual, "unit %p", self);
    return expected;
}
EI_VERIFY(0x005CB670, NearestEnemyVerify, 6, s_nearestEnemy);

// 005CBC80 ForceSee: takes references and logs, so ours only: nothing changes for an object
// already visible; else a corpse may join only +0x12C / the corpses (at their tails), a living
// unit only the visible list (at its head).
static void __fastcall ForceSeeVerify(std::uint8_t* memory, void*, CUnit* self, CEngineObject* object) {
    AIBrain* brain = BrainOfMemory(memory);
    bool wasVisible = brain->visible.Find(object) != nullptr;
    int visible = brain->visible.count, seen = brain->seenCorpses.count, corpses = brain->corpses.count;
    brain->ForceSee(self, object);
    int dv = brain->visible.count - visible, ds = brain->seenCorpses.count - seen, dc = brain->corpses.count - corpses;
    bool ok;
    if (wasVisible) {
        ok = dv == 0 && ds == 0 && dc == 0;
    } else {
        ok = (dv == 0 && ds == 0 && dc == 0) || (dv == 1 && ds == 0 && dc == 0 && brain->visible.head->data == object) ||
             (dv == 0 && ds == 1 && dc <= 1 && brain->seenCorpses.tail->data == object && (dc == 0 || brain->corpses.tail->data == object));
    }
    ei::verify::Check("AIBrain::ForceSee", true, ok, "unit %p object %p: %d %d %d", self, object, dv, ds, dc);
}
EI_VERIFY(0x005CBC80, ForceSeeVerify, 0, s_unused);

// 005CBE90 SeeCorpse: takes references and logs, so ours only: the corpse at the tail of +0x12C,
// and at the corpses' tail when that list grew.
static void __fastcall SeeCorpseVerify(std::uint8_t* memory, void*, CUnit* self, CEngineObject* corpse) {
    AIBrain* brain = BrainOfMemory(memory);
    int seen = brain->seenCorpses.count, corpses = brain->corpses.count;
    brain->SeeCorpse(self, corpse);
    int dc = brain->corpses.count - corpses;
    bool ok = brain->seenCorpses.count == seen + 1 && brain->seenCorpses.tail->data == corpse &&
              (dc == 0 || (dc == 1 && brain->corpses.tail->data == corpse));
    ei::verify::Check("AIBrain::SeeCorpse", true, ok, "unit %p corpse %p", self, corpse);
}
EI_VERIFY(0x005CBE90, SeeCorpseVerify, 0, s_unused);

// 005CB850 MeasureExposure: PUSH EBP; MOV EBP,ESP; SUB ESP,0x6C (6 bytes). Output: the original
// into a scratch array, ours into the unit's; the five floats within a few ULPs. A distance on a
// range's rounding edge could decide differently (rare).
static void(__fastcall* s_measureExposure)(std::uint8_t*, void*, UnitController*, float*);
static void __fastcall MeasureExposureVerify(std::uint8_t* memory, void*, UnitController* self, float* out) {
    float theirs[5];
    s_measureExposure(memory, nullptr, self, theirs);
    BrainOfMemory(memory)->MeasureExposure(self, out);
    for (int i = 0; i < 5; ++i)
        ei::verify::CheckNear("AIBrain::MeasureExposure", theirs[i], out[i], "unit %p [%d]: %f vs %f", self->unit, i, theirs[i], out[i]);
}
EI_VERIFY(0x005CB850, MeasureExposureVerify, 6, s_measureExposure);
