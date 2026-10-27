// The unit AI modes' lifetime and constant slots: the Player and Patrol constructors (005D3760,
// 005D0F60), the Patrol and Fear destructors, their
// GetType slots, the abstract base's Execute, the abstract bases'
// constructors and the patrol point array's destructors.
#include "unit_ai.h"
#include "combat.h"

#include "random.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"

#include <cstring>

namespace {
constexpr int kRemarkIdle = 1;
constexpr int kTypePatrol = 1;
constexpr int kTypeFear = 7;

// The patrol points' tables as compiled collections (mfc_slots.h) in place of game.exe's 0073FF74 (the
// constructors store it) and 0073FF80 (the destructors': the base's), which have the same slots: 1 the destructor
// (PatrolPointArray::DeletingDestruct, 005D8CD0 / its copy 005D9D00), 2 the 0x20-byte records' Serialize.
void __fastcall DestructPatrolPoints(PatrolPointArray* points) { points->Destruct(); }
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
EI_REWRITTEN_SPECIAL(0x005D8850, AIMode::AIMode);

AIActionMode::AIActionMode() {}
EI_REWRITTEN_SPECIAL(0x005D6D10, AIActionMode::AIActionMode);

void AIMode::Execute(AIBrain* brain) {
    if (IsaacRandom::Global().Next() % 1001 == 1) brain->owner->voice.Raise(kRemarkIdle);
    brain->attention.LookAround(brain, 0);
}
EI_REWRITTEN(0x005D0A90, AIMode::Execute);

int PatrolMode::GetType() { return kTypePatrol; }
EI_REWRITTEN(0x005D1210, PatrolMode::GetType);

int FearMode::GetType() { return kTypeFear; }
EI_REWRITTEN(0x005D2920, FearMode::GetType);

PlayerMode::PlayerMode() {
    target = nullptr;
    leader = nullptr;
    point = Vector3{0.0f, 0.0f, 0.0f};
    state = kStateFollow;
    run = 0;
}
EI_REWRITTEN_SPECIAL(0x005D3760, PlayerMode::PlayerMode);

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
EI_REWRITTEN_SPECIAL(0x005D0F60, PatrolMode::PatrolMode);

// Each point's looks freed (their base table), then the points (the array keeps its pointer and count).
PatrolMode::~PatrolMode() { points.Destruct(); }
EI_REWRITTEN_SPECIAL(0x005D1240, PatrolMode::~PatrolMode);
EI_REWRITTEN_SPECIAL(0x005D1220, PatrolMode::~PatrolMode);

// Destroyed and built again in place (the reference count kept).
int PatrolMode::OnShutdown() {
    points.Destruct();
    ClearPoints(points);
    current = 0;
    return 0;
}
EI_REWRITTEN(0x005D1140, PatrolMode::OnShutdown);

// 005D9E40 (and its thunk 005D9590; RET 8): point `index` copied into `out`: the position, then a
// new look array sized to the source's (new looks zeroed) with each look copied.
PatrolPoint* PatrolPointArray::CopyAt(PatrolPoint* out, int index) const {
    const PatrolPoint& source = data[index];
    std::memcpy(&out->position, &source.position, sizeof(Vector3));
    out->looks.Construct(mfc::impl::AiLookRecordArrayClass::Vtable());
    out->looks.SetSize(source.looks.size, -1);
    for (int i = 0; i < source.looks.size; ++i) out->looks.data[i] = source.looks.data[i];
    return out;
}
EI_REWRITTEN(0x005D9E40, PatrolPointArray::CopyAt);
EI_REWRITTEN(0x005D9590, PatrolPointArray::CopyAt);

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
EI_REWRITTEN(0x005D0F90, PatrolMode::Clone);

FearMode* FearMode::Clone() {
    auto* clone = new FearMode(flag);
    if (!clone) return nullptr;
    clone->distance = distance;
    clone->spread = spread;
    clone->shelter = shelter;
    clone->fleePoint = fleePoint;
    return clone;
}
EI_REWRITTEN(0x005D2880, FearMode::Clone);

int FearMode::OnShutdown() {
    Init(0);
    return 0;
}
EI_REWRITTEN(0x005D28F0, FearMode::OnShutdown);

FearMode::~FearMode() {}
EI_REWRITTEN_SPECIAL(0x005D2950, FearMode::~FearMode);
EI_REWRITTEN_SPECIAL(0x005D2930, FearMode::~FearMode);

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
EI_REWRITTEN(0x005D8CF0, PatrolPointArray::Destruct);
EI_REWRITTEN(0x005D95B0, PatrolPointArray::Destruct);

PatrolPointArray* PatrolPointArray::DeletingDestruct(int flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x005D8CD0, PatrolPointArray::DeletingDestruct);
EI_REWRITTEN(0x005D9D00, PatrolPointArray::DeletingDestruct);   // byte-identical copy (A-W12)

