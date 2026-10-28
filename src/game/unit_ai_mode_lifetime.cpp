// The unit AI modes' lifetime and constant slots: the Player and Patrol constructors
// the Patrol and Fear destructors, their
// GetType slots, the abstract base's Execute, the abstract bases'
// constructors and the patrol point array's destructors.
#include "unit_ai.h"
#include "combat.h"

#include "random.h"

#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"

#include <cstring>

namespace {
constexpr int kRemarkIdle = 1;
constexpr int kTypePatrol = 1;
constexpr int kTypeFear = 7;

// The patrol points' tables as compiled collections (mfc_slots.h) in place (the constructors store it; the
// destructors store the base's), which have the same virtuals: the destructor
// (PatrolPointArray::DeletingDestruct and its copy) and the 0x20-byte records' Serialize.
void DestructPatrolPoints(PatrolPointArray* points) { points->Destruct(); }
struct PatrolPointsBaseClass : mfc::impl::CompiledCollection<PatrolPointsBaseClass, &DestructPatrolPoints,
                                                             &mfc::impl::AiLookRecords::Serialize> {};
struct PatrolPointsClass final : PatrolPointsBaseClass {
    static void* Vtable() { return mfc::impl::CompiledVtable<PatrolPointsClass>(); }
};
} // namespace

AIMode::AIMode() {
    refs_ = 0;
    flags6_ = 0;
    flags7_ = 0;
}

AIActionMode::AIActionMode() {}

void AIMode::Execute(AIBrain* brain) {
    if (IsaacRandom::Global().Next() % 1001 == 1) brain->owner->voice.Raise(kRemarkIdle);
    brain->attention.LookAround(brain, 0);
}

int PatrolMode::GetType() { return kTypePatrol; }

int FearMode::GetType() { return kTypeFear; }

PlayerMode::PlayerMode() {
    target = nullptr;
    leader = nullptr;
    point = Vector3{0.0f, 0.0f, 0.0f};
    state = kStateFollow;
    run = 0;
}

namespace {
// A patrol mode's points as the constructor leaves them: none, the constructors' table.
void ClearPoints(PatrolPointArray& points) {
    points.data = nullptr;
    points.growBy = 0;
    points.maxSize = 0;
    points.size = 0;
    points.vtable = PatrolPointsClass::Vtable();
}
} // namespace

PatrolMode::PatrolMode() {
    ClearPoints(points);
    current = 0;
}

// Each point's looks freed (their base table), then the points (the array keeps its pointer and count).
PatrolMode::~PatrolMode() { points.Destruct(); }

// Destroyed and built again in place (the reference count kept).
int PatrolMode::OnShutdown() {
    points.Destruct();
    ClearPoints(points);
    current = 0;
    return 0;
}

// Point `index` copied into `out`: the position, then a
// new look array sized to the source's (new looks zeroed) with each look copied.
PatrolPoint* PatrolPointArray::CopyAt(PatrolPoint* out, int index) const {
    const PatrolPoint& source = data[index];
    std::memcpy(&out->position, &source.position, sizeof(Vector3));
    out->looks.Construct(mfc::impl::AiLookRecordArrayClass::Vtable());
    out->looks.SetSize(source.looks.size, -1);
    for (int i = 0; i < source.looks.size; ++i) out->looks.data[i] = source.looks.data[i];
    return out;
}

// A copy with its own points, each through a temporary copy of the source point
// (PatrolPointArray::CopyAt) whose looks are freed after.
PatrolMode* PatrolMode::Clone() {
    auto* clone = new PatrolMode();
    if (!clone) return nullptr;
    clone->points.SetSize(points.size, -1);
    for (int i = 0; i < points.size; ++i) {
        PatrolPoint temporary;
        points.CopyAt(&temporary, i);
        PatrolPoint& target = clone->points.data[i];
        std::memcpy(&target.position, &temporary.position, sizeof(Vector3));
        target.looks.SetSize(temporary.looks.size, -1);
        for (int j = 0; j < temporary.looks.size; ++j) {
            AILook look;
            temporary.looks.CopyAt(&look, j);
            target.looks.data[j] = look;
        }
        temporary.looks.vtable = mfc::impl::AiLookArrayClass::Vtable();
        if (temporary.looks.data) gamecrt::operator_delete(temporary.looks.data);
    }
    clone->current = current;
    return clone;
}

FearMode* FearMode::Clone() {
    auto* clone = new FearMode(flag);
    if (!clone) return nullptr;
    clone->distance = distance;
    clone->spread = spread;
    clone->shelter = shelter;
    clone->fleePoint = fleePoint;
    return clone;
}

int FearMode::OnShutdown() {
    Init(0);
    return 0;
}

FearMode::~FearMode() {}

void PatrolPointArray::Destruct() {
    vtable = PatrolPointsBaseClass::Vtable();
    if (!data) return;
    for (int i = 0; i < size; ++i) {
        AILookArray& looks = data[i].looks;
        looks.vtable = mfc::impl::AiLookArrayClass::Vtable();
        if (looks.data) gamecrt::operator_delete(looks.data);
    }
    gamecrt::operator_delete(data);
}

PatrolPointArray* PatrolPointArray::DeletingDestruct(int flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
