// The unit AI modes' lifetime and constant slots: the Player and Patrol constructors (005D3760,
// 005D0F60), the Patrol and Fear destructors (005D1240 / 005D1220, 005D2950 / 005D2930), their
// GetType slots (005D1210, 005D2920), the abstract base's Execute (005D0A90), the abstract bases'
// constructors (005D8850, 005D6D10), the patrol point array's destructors (005D8CF0, 005D8CD0) and
// two list / array template helpers (005D9830, 005D9E10).
#include "unit_ai.h"
#include "combat.h"

#include "random.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_ai.h"
#include "lib/mfc_array.h"

#include <cstring>

namespace {
constexpr int kRemarkIdle = 1;
constexpr int kTypePatrol = 1;
constexpr int kTypeFear = 7;
} // namespace

void AIMode::LookAroundExecute(AIBrain* brain) {
    if (IsaacRandom::Global().Next() % 1001 == 1) brain->owner->voice.Raise(kRemarkIdle);
    brain->LookAround(0);
}
EI_REPLACE_METHOD(0x005D0A90, AIMode::LookAroundExecute);

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
            looks.vtable = mfc::impl::AiLookArrayClass::Vtable();
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

// 005D1140 (slot 2): destroyed and built again in place (the reference count kept).
void PatrolMode::Rebuild() {
    Destruct();
    Construct();
}
EI_REPLACE_METHOD(0x005D1140, PatrolMode::Rebuild);

// 005D9E40 (and its thunk 005D9590; RET 8): point `index` copied into `out`: the position, then a
// new look array sized to the source's (new looks zeroed) with each look copied.
PatrolPoint* __fastcall PatrolPointCopyAt(const PatrolPointArray* points, void*, PatrolPoint* out, int index) {
    const PatrolPoint& source = points->data[index];
    std::memcpy(&out->position, &source.position, sizeof(Vector3));
    out->looks.Construct(mfc::impl::AiLookRecordArrayClass::Vtable());
    out->looks.SetSize(source.looks.size, -1);
    for (int i = 0; i < source.looks.size; ++i) out->looks.data[i] = source.looks.data[i];
    return out;
}
EI_REPLACE(0x005D9E40, PatrolPointCopyAt);
EI_REPLACE(0x005D9590, PatrolPointCopyAt);

// 005D0F90 (slot 3): a copy with its own points. The reference count dword is cleared before the
// allocation's null test; the abstract base's vtable stands while the points are copied, each
// through a temporary copy of the source point (PatrolPointCopyAt) whose looks are freed after.
PatrolMode* PatrolMode::Clone() {
    auto* clone = static_cast<PatrolMode*>(gamecrt::operator_new(sizeof(PatrolMode)));
    std::memset(&clone->refs, 0, 4);
    if (!clone) return nullptr;
    clone->vtable = aimode::kBaseVtable;
    clone->points.data = nullptr;
    clone->points.growBy = 0;
    clone->points.maxSize = 0;
    clone->points.size = 0;
    clone->points.vtable = aimode::kPatrolPointsVtable;
    PatrolPointArraySetSizeOriginal(&clone->points, nullptr, points.size, -1);
    for (int i = 0; i < points.size; ++i) {
        PatrolPoint temporary;
        PatrolPointCopyAt(&points, nullptr, &temporary, i);
        PatrolPoint& target = clone->points.data[i];
        std::memcpy(&target.position, &temporary.position, sizeof(Vector3));
        LooksSetSizeAt(&target.looks, nullptr, temporary.looks.size, -1);
        for (int j = 0; j < temporary.looks.size; ++j) {
            AILook look;
            LooksElementAt(&temporary.looks, nullptr, &look, j);
            target.looks.data[j] = look;
        }
        temporary.looks.vtable = mfc::impl::AiLookArrayClass::Vtable();
        if (temporary.looks.data) gamecrt::operator_delete(temporary.looks.data);
    }
    clone->current = current;
    clone->vtable = aimode::kPatrolVtable;
    return clone;
}
EI_REPLACE_METHOD(0x005D0F90, PatrolMode::Clone);

FearMode* FearMode::Clone() {
    auto* clone = static_cast<FearMode*>(gamecrt::operator_new(sizeof(FearMode)));
    std::memset(&clone->refs, 0, 4);
    if (!clone) return nullptr;
    clone->vtable = aimode::kActionBaseVtable;
    clone->distance = distance;
    clone->spread = spread;
    clone->shelter = shelter;
    clone->fleePoint = fleePoint;
    clone->flag = flag;
    clone->vtable = aimode::kFearVtable;
    return clone;
}
EI_REPLACE_METHOD(0x005D2880, FearMode::Clone);

int FearMode::Reset() {
    Destruct();
    Construct(0);
    return 0;
}
EI_REPLACE_METHOD(0x005D28F0, FearMode::Reset);

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
        looks.vtable = mfc::impl::AiLookArrayClass::Vtable();
        if (looks.data) gamecrt::operator_delete(looks.data);
    }
    gamecrt::operator_delete(points->data);
}
EI_REPLACE(0x005D8CF0, PatrolPointsDestruct);
EI_REPLACE(0x005D95B0, PatrolPointsDestruct);   // byte-identical copy (A-W12)

PatrolPointArray* __fastcall PatrolPointsDeletingDestruct(PatrolPointArray* points, void*, int flags) {
    PatrolPointsDestruct(points);
    if (flags & 1) gamecrt::operator_delete(points);
    return points;
}
EI_REPLACE(0x005D8CD0, PatrolPointsDeletingDestruct);
EI_REPLACE(0x005D9D00, PatrolPointsDeletingDestruct);   // byte-identical copy (A-W12)

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
