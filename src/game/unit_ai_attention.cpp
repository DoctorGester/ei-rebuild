// The unit AI brain's attention: what it attacks now and looking for a better target
#include "unit_ai.h"
#include "combat.h"

#include "world.h"


#include <cstddef>
#include <cstdint>

namespace {

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

void AIAttention::LookAround(AIBrain* brain, int reachArg) {
    CountedUnits candidates{};
    AITargetChoice best{-1, nullptr};
    Vector3 from = *&brain->owner->unit->localPosition_;
    brain->ChooseTarget(&best, &candidates, &from, 1, reachArg);
    CUnit* chosen = best.object;
    if (chosen) {
        AITargetChoice now{-1, nullptr};
        CurrentAttack(brain, &now);
        if (best.object != now.object || best.part != now.part) {
            int part = best.part;
            if (part == -1) {
                brain->action = AIBrain::kActionAttack;
                brain->target.Assign(chosen);
                AckKind flags = AckKind::DecidedToAttack;
                if (brain->owner->unit->player) {
                    AIOrder order{};
                    order.kind = UnitOrderKind::Attack;
                    order.source = 6;
                    ++chosen->refCount_;
                    order.target = chosen;
                    if (brain->owner->CanReachAndPlan(&order)) flags = AckKind::None;
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
    // The list emptied, no element destruction.
    candidates.clear();
}
