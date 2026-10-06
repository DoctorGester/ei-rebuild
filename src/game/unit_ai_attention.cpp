// The unit AI brain's attention (+0xA4): what it attacks now and looking for a better target
// (005CF220, 005CFE40).
#include "unit_ai.h"

#include "world.h"

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

namespace {
// The order the player's check (0054C8B0) is asked about before an attack (AIOrder; the original
// clears it with 00545F40 and makes it an attack (3, 6) on the target inline).
using AttackOrder = AIOrder;

// Holds `object` in `ref` with a counted reference (AIRef::Assign, 0052A8F0).
void Hold(CEngineObject*& ref, CEngineObject* object) { reinterpret_cast<AIRef*>(&ref)->Assign(object); }
} // namespace

void AIBrain::CurrentAttack(int* partAndObject) {
    auto*& object = reinterpret_cast<CEngineObject*&>(partAndObject[1]);
    if (action == kActionAttack) {
        partAndObject[0] = -1;
    } else if (action == kActionCast) {
        partAndObject[0] = actionTime;     // the spell being cast
    } else {
        object = nullptr;
        return;
    }
    object = target;
    if (object && object->GetClassId() != kClassUnit) object = nullptr;
}

void __fastcall CurrentAttackHook(std::uint8_t* attention, void*, AIBrain* brain, int* partAndObject) {
    (void)attention;    // always the brain's +0xA4
    brain->CurrentAttack(partAndObject);
}
EI_REPLACE(0x005CF220, CurrentAttackHook);

void AIBrain::LookAround(int unused) {
    mfc::CTypedPtrList<CEngineObject> candidates{};
    candidates.vtable = mfc::PtrListVtable();
    candidates.blockSize = 10;
    int best[2] = {-1, 0};          // {part, object}
    Vector3 from = *reinterpret_cast<const Vector3*>(&owner->unit->positionX);
    ChooseTarget(best, &candidates, &from, 1, unused);
    auto* chosen = reinterpret_cast<CEngineObject*>(best[1]);
    if (chosen) {
        int now[2] = {-1, 0};
        CurrentAttack(now);
        if (best[1] != now[1] || best[0] != now[0]) {
            int part = best[0];
            if (part == -1) {
                action = kActionAttack;
                Hold(target, chosen);
                int flags = 0x1C;
                if (owner->unit->player) {
                    AttackOrder order{};
                    order.kind = 3;
                    order.what = 6;
                    ++chosen->refCount_;
                    order.object = chosen;
                    if (owner->CheckOrder(&order)) flags = -1;
                    reinterpret_cast<AIRef*>(&order.object)->Reset();
                }
                owner->Attack(target, 6, 0, flags);
            } else {
                if (!owner->CanCast(part)) action = kActionCast;
                Hold(target, chosen);
                actionTime = part;
                owner->Cast(part, chosen, 0);
            }
        }
    }
    // The list's RemoveAll: no element destruction.
    candidates.RemoveAll();
}

void __fastcall LookAroundHook(std::uint8_t* attention, void*, AIBrain* brain, int unused) {
    (void)attention;    // always the brain's +0xA4
    brain->LookAround(unused);
}
EI_REPLACE(0x005CFE40, LookAroundHook);
