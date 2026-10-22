// The unit AI modes' lifetime and constant slots: the Player and Patrol constructors (005D3760,
// 005D0F60), the Patrol and Fear destructors (005D1240 / 005D1220, 005D2950 / 005D2930), their
// GetType slots (005D1210, 005D2920), the abstract base's Execute (005D0A90), the abstract bases'
// constructors (005D8850, 005D6D10), the patrol point array's destructors (005D8CF0, 005D8CD0) and
// two list / array template helpers (005D9830, 005D9E10).
#include "ai_mode_class.h"
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

// The patrol points' tables as compiled collections (C19, mfc_slots.h) in place of game.exe's 0073FF74 (the
// constructors store it) and 0073FF80 (the destructors': the base's), which have the same slots: 1 the destructor
// (PatrolPointArray::DeletingDestruct, 005D8CD0 / its copy 005D9D00), 2 the 0x20-byte records' Serialize (005D9610).
void __fastcall DestructPatrolPoints(PatrolPointArray* points) { points->Destruct(); }
struct PatrolPointsBaseClass : mfc::impl::CompiledCollection<PatrolPointsBaseClass, &DestructPatrolPoints,
                                                             &mfc::impl::AiLookRecords::Serialize> {};
struct PatrolPointsClass final : PatrolPointsBaseClass {
    static void* Vtable() { return mfc::impl::CompiledVtable<PatrolPointsClass>(); }
};
} // namespace

void AIMode::LookAroundExecute(AIBrain* brain) {
    if (IsaacRandom::Global().Next() % 1001 == 1) brain->owner->voice.Raise(kRemarkIdle);
    brain->attention.LookAround(brain, 0);
}
EI_REWRITTEN(0x005D0A90, AIMode::LookAroundExecute);

int __fastcall aimode::PatrolType(AIMode*) { return kTypePatrol; }
EI_REWRITTEN(0x005D1210, aimode::PatrolType);

int __fastcall aimode::FearType(AIMode*) { return kTypeFear; }
EI_REWRITTEN(0x005D2920, aimode::FearType);

PlayerMode* PlayerMode::Construct() {
    target = nullptr;
    leader = nullptr;
    point = Vector3{0.0f, 0.0f, 0.0f};
    vtable = aimode::PlayerClass::Vtable();
    state = kStateFollow;
    run = 0;
    return this;
}
EI_REWRITTEN(0x005D3760, PlayerMode::Construct);

PatrolMode* PatrolMode::Construct() {
    points.data = nullptr;
    points.growBy = 0;
    points.maxSize = 0;
    points.size = 0;
    points.vtable = PatrolPointsClass::Vtable();
    vtable = aimode::PatrolClass::Vtable();
    current = 0;
    return this;
}
EI_REWRITTEN(0x005D0F60, PatrolMode::Construct);

void PatrolMode::Destruct() {
    vtable = aimode::PatrolClass::Vtable();
    points.vtable = PatrolPointsBaseClass::Vtable();
    if (points.data) {
        // The elements' destructors: each look array's base destructor.
        for (int i = 0; i < points.size; ++i) {
            AILookArray& looks = points[i].looks;
            looks.vtable = mfc::impl::AiLookArrayClass::Vtable();
            if (looks.data) gamecrt::operator_delete(looks.data);
        }
        gamecrt::operator_delete(points.data);
    }
    vtable = CRefCounted::RootVtable();
}
EI_REWRITTEN(0x005D1240, PatrolMode::Destruct);

PatrolMode* PatrolMode::DeletingDestruct(int flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x005D1220, PatrolMode::DeletingDestruct);

// 005D1140 (slot 2): destroyed and built again in place (the reference count kept).
void PatrolMode::Rebuild() {
    Destruct();
    Construct();
}
EI_REWRITTEN(0x005D1140, PatrolMode::Rebuild);

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

// 005D0F90 (slot 3): a copy with its own points. The reference count dword is cleared before the
// allocation's null test; the abstract base's vtable stands while the points are copied, each
// through a temporary copy of the source point (PatrolPointArray::CopyAt) whose looks are freed after.
PatrolMode* PatrolMode::Clone() {
    auto* clone = gamecrt::operator_new_as<PatrolMode>();
    std::memset(&clone->refs, 0, 4);
    if (!clone) return nullptr;
    clone->vtable = aimode::ModeBaseClass::Vtable();
    clone->points.data = nullptr;
    clone->points.growBy = 0;
    clone->points.maxSize = 0;
    clone->points.size = 0;
    clone->points.vtable = PatrolPointsClass::Vtable();
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
    clone->vtable = aimode::PatrolClass::Vtable();
    return clone;
}
EI_REWRITTEN(0x005D0F90, PatrolMode::Clone);

FearMode* FearMode::Clone() {
    auto* clone = gamecrt::operator_new_as<FearMode>();
    std::memset(&clone->refs, 0, 4);
    if (!clone) return nullptr;
    clone->vtable = aimode::ActionBaseClass::Vtable();
    clone->distance = distance;
    clone->spread = spread;
    clone->shelter = shelter;
    clone->fleePoint = fleePoint;
    clone->flag = flag;
    clone->vtable = aimode::FearClass::Vtable();
    return clone;
}
EI_REWRITTEN(0x005D2880, FearMode::Clone);

int FearMode::Reset() {
    Destruct();
    Construct(0);
    return 0;
}
EI_REWRITTEN(0x005D28F0, FearMode::Reset);

void FearMode::Destruct() { vtable = CRefCounted::RootVtable(); }
EI_REWRITTEN(0x005D2950, FearMode::Destruct);

FearMode* FearMode::DeletingDestruct(int flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x005D2930, FearMode::DeletingDestruct);

AIMode* __fastcall aimode::ConstructModeBase(AIMode* mode) {
    mode->vtable = ModeBaseClass::Vtable();
    return mode;
}
EI_REWRITTEN(0x005D8850, aimode::ConstructModeBase);

AIMode* __fastcall aimode::ConstructActionBase(AIMode* mode) {
    mode->vtable = ActionBaseClass::Vtable();
    return mode;
}
EI_REWRITTEN(0x005D6D10, aimode::ConstructActionBase);

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

