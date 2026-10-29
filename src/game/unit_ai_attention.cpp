// The unit AI brain's attention: what it attacks now, looking for a better target and taking a chosen one up.
#include "combat.h"
#include "unit_ai.h"
#include "world.h"

AITargetChoice AIAttention::CurrentAttack(AIBrain* brain) {
    AITargetChoice choice;
    if (brain->action == AIBrain::kActionAttack) {
        choice.part = kWeaponPart;
    } else if (brain->action == AIBrain::kActionCast) {
        choice.part = brain->actionTime;     // the spell being cast
    } else {
        return choice;
    }
    choice.object = UnitOrNull(brain->target);
    return choice;
}

void AIAttention::LookAround(AIBrain* brain) {
    UnitList candidates;
    Vector3 from = brain->owner->unit->localPosition_;
    AITargetChoice best = Choose(brain, &candidates, from, true, false);
    ApplyAttack(brain, &best);
}

void AIAttention::ApplyAttack(AIBrain* brain, const AITargetChoice* choice) {
    CUnit* object = choice->object;
    if (!object) return;
    AITargetChoice now = CurrentAttack(brain);
    if (object == now.object && choice->part == now.part) return;
    if (choice->part == kWeaponPart)
        brain->AttackTarget(object);
    else
        brain->CastOn(choice->part, object, 0);
}
