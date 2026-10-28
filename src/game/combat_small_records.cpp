// Leftovers of the unit server controller's range (A-U12): a path's and a motion's part
// constructors, the armour spell record's assignment and the world's tick.
#include "combat.h"

#include "ai_map.h"      // AIPath, PathCellList
#include "ai_motion.h"   // UnitMotion
#include "unit_ai.h"     // AIRef, AICallRecord
#include "world.h"

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
    MotionCubic curveX;
    MotionCubic curveY;
    float segmentStart;
    float segmentEnd;
    int segment;
};
static_assert(offsetof(MotionSegment, curveX) == offsetof(UnitMotion, curveX) - offsetof(UnitMotion, fromX));
static_assert(offsetof(MotionSegment, segment) == offsetof(UnitMotion, segment) - offsetof(UnitMotion, fromX));
} // namespace

int __fastcall WorldTick(CWorld* world) { return world->tick_; }

ArmourSpell* ArmourSpell::Assign(const ArmourSpell* other) {
    CEngineObject* old = item;
    item = other->item;
    if (item) ++item->refCount_;
    if (old) old->Release();
    countdown = other->countdown;
    worn = other->worn;
    return this;
}

// The counted reference's and the call record's deleting destructors are in world_deleting_destructors.cpp.
