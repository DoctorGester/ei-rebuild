// Leftovers of the unit server controller's range (A-U12): a path's and a motion's part
// constructors, the armour spell record's assignment and the world's tick.
#include "combat.h"

#include "ai_map.h"      // AIPath, PathCellList
#include "ai_motion.h"   // UnitMotion
#include "unit_ai.h"     // AIRef, AICallRecord
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"

namespace {

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
} // namespace

// 0054B220 (thiscall on the world): its time in ticks. Four bytes (too short to hook).
int __fastcall WorldTick(CWorld* world) { return world->tick_; }
EI_REPLACE(0x0054B220, WorldTick);

ArmourSpell* ArmourSpell::Assign(const ArmourSpell* other) {
    CEngineObject* old = item;
    item = other->item;
    if (item) ++item->refCount_;
    if (old) old->Release();
    countdown = other->countdown;
    worn = other->worn;
    return this;
}

// 00558CA0 / 00558CC0 (the counted reference and the call record deleting destructors) are in
// world_deleting_destructors.cpp.
