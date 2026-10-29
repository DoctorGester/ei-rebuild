// Leftovers of the unit server controller's range (A-U12): a path's and a motion's part
// constructors, the armour spell record's assignment and the world's tick.
#include "combat.h"

#include "ai_map.h"      // AIPath, PathCellList
#include "ai_motion.h"   // UnitMotion
#include "unit_ai.h"     // AICallRecord
#include "world.h"

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
} // namespace

ArmourSpell* ArmourSpell::Assign(const ArmourSpell* other) {
    item.Assign(other->item);
    countdown = other->countdown;
    worn = other->worn;
    return this;
}

