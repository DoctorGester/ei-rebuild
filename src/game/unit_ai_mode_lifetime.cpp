// The unit AI modes' lifetime and constant slots: the Player and Patrol constructors (005D3760,
// 005D0F60), the Patrol and Fear destructors (005D1240 / 005D1220, 005D2950 / 005D2930), their
// GetType slots (005D1210, 005D2920) and the abstract base's Execute (005D0A90).
#include "unit_ai.h"

#include "random.h"

#include "ei/hook.h"
#include "lib/crt.h"

namespace {
constexpr int kRemarkIdle = 1;
constexpr int kTypePatrol = 1;
constexpr int kTypeFear = 7;
} // namespace

void __fastcall aimode::LookAroundExecute(AIMode*, void*, AIBrain* brain) {
    if (IsaacRandom::Global().Next() % 1001 == 1) brain->owner->Say(kRemarkIdle);
    brain->LookAround(0);
}
EI_REPLACE(0x005D0A90, aimode::LookAroundExecute);

int __fastcall aimode::PatrolType(AIMode*) { return kTypePatrol; }
EI_REPLACE(0x005D1210, aimode::PatrolType);

int __fastcall aimode::FearType(AIMode*) { return kTypeFear; }
EI_REPLACE(0x005D2920, aimode::FearType);

PlayerMode* PlayerMode::Construct() {
    target = nullptr;
    leader = nullptr;
    point = Vector3{0.0f, 0.0f, 0.0f};
    vtable = aimode::kPlayerVtable;
    state = kStateFollow;
    run = 0;
    return this;
}
EI_REPLACE_METHOD(0x005D3760, PlayerMode::Construct);

PatrolMode* PatrolMode::Construct() {
    points = nullptr;
    pointGrowBy = 0;
    pointMax = 0;
    pointCount = 0;
    pointsVtable = aimode::kPatrolPointsVtable;
    vtable = aimode::kPatrolVtable;
    current = 0;
    return this;
}
EI_REPLACE_METHOD(0x005D0F60, PatrolMode::Construct);

void PatrolMode::Destruct() {
    vtable = aimode::kPatrolVtable;
    pointsVtable = aimode::kPatrolPointsBaseVtable;
    if (points) {
        // The elements' destructors: each look array's base destructor.
        for (int i = 0; i < pointCount; ++i) {
            AILookArray& looks = points[i].looks;
            looks.vtable = reinterpret_cast<void*>(kLookArrayBaseVtable);
            if (looks.data) gamecrt::operator_delete(looks.data);
        }
        gamecrt::operator_delete(points);
    }
    vtable = aimode::kRootVtable;
}
EI_REPLACE_METHOD(0x005D1240, PatrolMode::Destruct);

PatrolMode* PatrolMode::DeletingDestruct(int flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005D1220, PatrolMode::DeletingDestruct);

void FearMode::Destruct() { vtable = aimode::kRootVtable; }
EI_REPLACE_METHOD(0x005D2950, FearMode::Destruct);

FearMode* FearMode::DeletingDestruct(int flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005D2930, FearMode::DeletingDestruct);
