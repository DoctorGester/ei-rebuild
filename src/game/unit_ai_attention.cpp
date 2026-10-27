// The unit AI brain's attention (+0xA4): what it attacks now and looking for a better target
#include "unit_ai.h"
#include "combat.h"

#include "world.h"

#include "ei/hook.h"
#include "lib/mfc_slots.h"

#include <cstddef>
#include <cstdint>

namespace {
// The order the player's check is asked about before an attack (AIOrder; the original
// clears it with makes it an attack (3, 6) on the target inline).
using AttackOrder = AIOrder;

} // namespace

void AIAttention::CurrentAttack(AIBrain* brain, AITargetChoice* choice) {
    if (brain->action == AIBrain::kActionAttack) {
        choice->part = -1;
    } else if (brain->action == AIBrain::kActionCast) {
        choice->part = brain->actionTime;     // the spell being cast
    } else {
        choice->object = nullptr;
        return;
    }
    choice->object = UnitOrNull(brain->target);
}
EI_REWRITTEN(0x005CF220, AIAttention::CurrentAttack);

void AIAttention::LookAround(AIBrain* brain, int unused) {
    mfc::CTypedPtrList<CUnit> candidates{};
    candidates.vtable = mfc::impl::PtrListClass::Vtable();
    candidates.blockSize = 10;
    AITargetChoice best{-1, nullptr};
    Vector3 from = *&brain->owner->unit->localPosition_;
    brain->ChooseTarget(&best, &candidates, &from, 1, unused);
    CUnit* chosen = best.object;
    if (chosen) {
        AITargetChoice now{-1, nullptr};
        CurrentAttack(brain, &now);
        if (best.object != now.object || best.part != now.part) {
            int part = best.part;
            if (part == -1) {
                brain->action = AIBrain::kActionAttack;
                brain->target.Assign(chosen);
                int flags = 0x1C;
                if (brain->owner->unit->player) {
                    AttackOrder order{};
                    order.kind = 3;
                    order.source = 6;
                    ++chosen->refCount_;
                    order.target = chosen;
                    if (brain->owner->CanReachAndPlan(&order)) flags = -1;
                    order.target.Reset();
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
