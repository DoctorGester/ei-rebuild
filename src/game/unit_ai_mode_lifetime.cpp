// The unit AI modes' lifetime and constant slots: the Player and Patrol constructors (005D3760,
// 005D0F60), the Patrol and Fear destructors (005D1240 / 005D1220, 005D2950 / 005D2930), their
// GetType slots (005D1210, 005D2920), the abstract base's Execute (005D0A90), the abstract bases'
// constructors (005D8850, 005D6D10), the patrol point array's destructors (005D8CF0, 005D8CD0) and
// two list / array template helpers (005D9830, 005D9E10).
#include "unit_ai.h"

#include "random.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

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
    points.data = nullptr;
    points.growBy = 0;
    points.maxSize = 0;
    points.size = 0;
    points.vtable = aimode::kPatrolPointsVtable;
    vtable = aimode::kPatrolVtable;
    current = 0;
    return this;
}
EI_REPLACE_METHOD(0x005D0F60, PatrolMode::Construct);

void PatrolMode::Destruct() {
    vtable = aimode::kPatrolVtable;
    points.vtable = aimode::kPatrolPointsBaseVtable;
    if (points.data) {
        // The elements' destructors: each look array's base destructor.
        for (int i = 0; i < points.size; ++i) {
            AILookArray& looks = points[i].looks;
            looks.vtable = reinterpret_cast<void*>(kLookArrayBaseVtable);
            if (looks.data) gamecrt::operator_delete(looks.data);
        }
        gamecrt::operator_delete(points.data);
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

AIMode* __fastcall aimode::ConstructModeBase(AIMode* mode) {
    mode->vtable = kBaseVtable;
    return mode;
}
EI_REPLACE(0x005D8850, aimode::ConstructModeBase);

AIMode* __fastcall aimode::ConstructActionBase(AIMode* mode) {
    mode->vtable = kActionBaseVtable;
    return mode;
}
EI_REPLACE(0x005D6D10, aimode::ConstructActionBase);

void __fastcall PatrolPointsDestruct(PatrolPointArray* points) {
    points->vtable = aimode::kPatrolPointsBaseVtable;
    if (!points->data) return;
    for (int i = 0; i < points->size; ++i) {
        AILookArray& looks = points->data[i].looks;
        looks.vtable = reinterpret_cast<void*>(kLookArrayBaseVtable);
        if (looks.data) gamecrt::operator_delete(looks.data);
    }
    gamecrt::operator_delete(points->data);
}
EI_REPLACE(0x005D8CF0, PatrolPointsDestruct);

PatrolPointArray* __fastcall PatrolPointsDeletingDestruct(PatrolPointArray* points, void*, int flags) {
    PatrolPointsDestruct(points);
    if (flags & 1) gamecrt::operator_delete(points);
    return points;
}
EI_REPLACE(0x005D8CD0, PatrolPointsDeletingDestruct);

void __stdcall ZeroAttackOptions(AIAttackOption* first, int count) {
    std::memset(first, 0, sizeof(AIAttackOption) * static_cast<unsigned>(count));
}
EI_REPLACE(0x005D9E10, ZeroAttackOptions);

void* __stdcall ListGetNext(void** position) {
    auto* node = static_cast<AIObjectList::Node*>(*position);
    *position = node->next;
    return &node->data;
}
EI_REPLACE(0x005D9830, ListGetNext);
