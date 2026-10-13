// Leftovers of the unit server controller's range (A-U12): the order checks that refuse targets
// too high above the ground (0054C8B0 / 0054C960), a path's and a motion's part constructors, the
// armour spell record's assignment, two records' deleting destructors and the world's tick.
// Plain C++ float maths (rule 7).
#include "combat.h"

#include "ai_map.h"      // AIPath, PathCellList
#include "ai_motion.h"   // UnitMotion
#include "item.h"        // CItemWeapon
#include "map_object.h"
#include "unit.h"
#include "unit_ai.h"     // AIRef, AICallRecord
#include "unit_db.h"     // UnitPrototypeRecord
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <bit>

namespace {
constexpr int kActionAttack = 3;
constexpr int kActionUse = 6;
constexpr int kWeaponBow = 5;          // CItemWeapon::GetType: the two ranged types
constexpr int kWeaponCrossbow = 6;
constexpr float kReachHeight = 1.5f;   // 0073C51C
constexpr int kPathBlockSize = 0x80;

// The segment part of a unit's motion (UnitMotion + 0x50 on), as its constructors take it.
struct MotionSegment {
    float fromX, fromY;
    float fromDirX, fromDirY;
    float toX, toY;
    float toDirX, toDirY;
    float length;
    float speed;
    MotionCubic curveX;     // +0x28
    MotionCubic curveY;     // +0x38
    float segmentStart;
    float segmentEnd;
    int segment;            // +0x50
};
static_assert(offsetof(MotionSegment, curveX) == offsetof(UnitMotion, curveX) - offsetof(UnitMotion, fromX));
static_assert(offsetof(MotionSegment, segment) == offsetof(UnitMotion, segment) - offsetof(UnitMotion, fromX));

// Whether `order` (an attack, or a use / skill) aims at a living unit of size class 0 that is too
// high to reach: its prototype's +0x24 plus its height at least 1.5. An attack with a bow or a
// crossbow is not refused here. (NaN heights are not refused either.)
bool TargetTooHigh(const CUnitServer* controller, const UnitAction* order) {
    if (order->kind != kActionAttack && order->kind != kActionUse) return false;
    CEngineObject* target = order->target;
    if (!target) return false;
    if (target->GetClassId() != kClassUnit) return false;
    if (ObjectSlots(target)->IsDead() != 0) return false;
    auto* unit = static_cast<CUnit*>(target);
    if (unit->sizeClass != 0) return false;
    if (order->kind == kActionAttack) {
        CUnit* self = controller->unit;
        auto* weapon = static_cast<CItemWeapon*>(self->GetWeapon(self->activeWeapon));
        if (weapon) {
            if (weapon->GetType() == kWeaponBow) return false;
            if (weapon->GetType() == kWeaponCrossbow) return false;
        }
    }
    const auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype());
    const float height = std::bit_cast<float>(prototype->unknown_24) + unit->position.z;
    return height >= kReachHeight;
}
} // namespace

bool CUnitServer::CheckOrder(const UnitAction* order) {
    if (TargetTooHigh(this, order)) return false;
    return CallCanPlanAction(order);
}
EI_REPLACE_METHOD(0x0054C8B0, CUnitServer::CheckOrder);

bool CUnitServer::CheckOrderFrom(const UnitAction* order, const Vector3* from) {
    if (TargetTooHigh(this, order)) return false;
    const Vector3 kept = unit->position;
    unit->position = *from;
    const bool result = CallCanPlanAction(order);
    unit->position = kept;
    return result;
}
EI_REPLACE_METHOD(0x0054C960, CUnitServer::CheckOrderFrom);

// 0054B220 (thiscall on the world): its time in ticks. Four bytes (too short to hook).
int __fastcall WorldTick(CWorld* world) { return world->tick_; }
EI_REPLACE(0x0054B220, WorldTick);

// 0054E5D0 (thiscall on a motion's segment part): both curves zeroed, no segment. Returns it.
void* __fastcall MotionSegmentInit(void* part) {
    auto* segment = static_cast<MotionSegment*>(part);
    segment->curveX = {};
    segment->curveY = {};
    segment->segment = -1;
    return segment;
}
EI_REPLACE(0x0054E5D0, MotionSegmentInit);

// 0054EAC0 (thiscall on a path): its first cell list made empty (the vtable 0073EDB0, blocks of
// 0x80). Returns the path.
AIPath* __fastcall PathConstructSteps(AIPath* path) {
    PathCellList& steps = path->steps;
    steps.vtable = PathCellListVtable();
    steps.count = 0;
    steps.free = nullptr;
    steps.tail = nullptr;
    steps.head = nullptr;
    steps.blocks = nullptr;
    steps.blockSize = kPathBlockSize;
    return path;
}
EI_REPLACE(0x0054EAC0, PathConstructSteps);

ArmourSpell* ArmourSpell::Assign(const ArmourSpell* other) {
    CEngineObject* old = item;
    item = other->item;
    if (item) ++item->refCount_;
    if (old) old->Release();
    countdown = other->countdown;
    worn = other->worn;
    return this;
}
EI_REPLACE_METHOD(0x00558840, ArmourSpell::Assign);

// 00558CA0 / 00558CC0 (RET 4): the scalar deleting destructors of a counted reference (00558CE0)
// and of a call record (00558D00), as the brain's memory arrays destroy their elements.
AIRef* __fastcall CountedRefDelete(AIRef* self, void*, unsigned flags) {
    self->Reset();
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00558CA0, CountedRefDelete);
AICallRecord* __fastcall CallRecordDelete(AICallRecord* self, void*, unsigned flags) {
    self->Destruct();
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00558CC0, CallRecordDelete);
