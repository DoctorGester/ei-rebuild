// The unit AI brain's attention (+0xA4): what it attacks now and looking for a better target
// (005CF220, 005CFE40).
#include "unit_ai.h"
#include "combat.h"

#include "world.h"

#include "ei/hook.h"
#include "lib/mfc_slots.h"

#include <cstddef>
#include <cstdint>

namespace {
// The order the player's check (0054C8B0) is asked about before an attack (AIOrder; the original
// clears it with 00545F40 and makes it an attack (3, 6) on the target inline).
using AttackOrder = AIOrder;

} // namespace

void AIAttention::CurrentAttack(AIBrain* brain, int* partAndObject) {
    auto*& object = reinterpret_cast<CEngineObject*&>(partAndObject[1]);
    if (brain->action == AIBrain::kActionAttack) {
        partAndObject[0] = -1;
    } else if (brain->action == AIBrain::kActionCast) {
        partAndObject[0] = brain->actionTime;     // the spell being cast
    } else {
        object = nullptr;
        return;
    }
    object = brain->target;
    if (object && object->GetClassId() != kClassUnit) object = nullptr;
}
EI_REWRITTEN(0x005CF220, AIAttention::CurrentAttack);

void AIAttention::LookAround(AIBrain* brain, int unused) {
    mfc::CTypedPtrList<CUnit> candidates{};
    candidates.vtable = mfc::impl::PtrListClass::Vtable();
    candidates.blockSize = 10;
    int best[2] = {-1, 0};          // {part, object}
    Vector3 from = *&brain->owner->unit->position;
    brain->ChooseTarget(best, &candidates, &from, 1, unused);
    auto* chosen = reinterpret_cast<CEngineObject*>(best[1]);
    if (chosen) {
        int now[2] = {-1, 0};
        CurrentAttack(brain, now);
        if (best[1] != now[1] || best[0] != now[0]) {
            int part = best[0];
            if (part == -1) {
                brain->action = AIBrain::kActionAttack;
                brain->target.Assign(chosen);
                int flags = 0x1C;
                if (brain->owner->unit->player) {
                    AttackOrder order{};
                    order.kind = 3;
                    order.what = 6;
                    ++chosen->refCount_;
                    order.object = chosen;
                    if (brain->owner->CanReachAndPlan(AsUnitAction(&order))) flags = -1;
                    order.object.Reset();
                }
                brain->owner->OrderAttack(brain->target, 6, 0, flags);
            } else {
                if (!brain->owner->IsScrollSource(part)) brain->action = AIBrain::kActionCast;
                brain->target.Assign(chosen);
                brain->actionTime = part;
                brain->owner->OrderCast(part, chosen, 0);
            }
        }
    }
    // The list's RemoveAll: no element destruction.
    candidates.RemoveAll();
}
EI_REWRITTEN(0x005CFE40, AIAttention::LookAround);
