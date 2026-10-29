// The unit AI brain's attention: what it attacks now, looking for a better target and taking a chosen one up.
#include "combat.h"
#include "unit_ai.h"
#include "world.h"

AITargetChoice AIBrain::CurrentAttack() {
    AITargetChoice choice;
    if (action == kActionAttack) {
        choice.part = kWeaponPart;
    } else if (action == kActionCast) {
        choice.part = actionTime;     // the spell being cast
    } else {
        return choice;
    }
    choice.object = UnitOrNull(target);
    return choice;
}

void AIBrain::LookAround() {
    UnitList candidates;
    Vector3 from = owner->unit->localPosition;
    AITargetChoice best = Choose(&candidates, from, true, false);
    ApplyAttack(&best);
}

void AIBrain::ApplyAttack(const AITargetChoice* choice) {
    CUnit* object = choice->object;
    if (!object) return;
    AITargetChoice now = CurrentAttack();
    if (object == now.object && choice->part == now.part) return;
    if (choice->part == kWeaponPart)
        AttackTarget(object);
    else
        CastOn(choice->part, object, 0);
}
