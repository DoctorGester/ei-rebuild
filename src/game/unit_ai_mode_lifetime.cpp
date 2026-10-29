// The unit AI modes' lifetime and constant slots: the Player and Patrol constructors
// the Patrol and Fear destructors, their
// GetType slots, the abstract base's Execute and the abstract bases'
// constructors.
#include "combat.h"
#include "random.h"
#include "unit_ai.h"

#include <cstring>

namespace {
} // namespace

AIMode::AIMode() {
    refs_ = 0;
    flags6_ = 0;
    flags7_ = 0;
}

AIActionMode::AIActionMode() {}

void AIMode::Execute(AIBrain* brain) {
    if (g_random.Next() % 1001 == 1) brain->owner->voice.Raise(AIRemark::Idle);
    brain->attention.LookAround(brain, 0);
}

AIModeType PatrolMode::GetType() { return AIModeType::Patrol; }

AIModeType FearMode::GetType() { return AIModeType::Fear; }

PlayerMode::PlayerMode() {
    point = Vector3{0.0f, 0.0f, 0.0f};
    state = kStateFollow;
    run = 0;
}

PatrolMode::PatrolMode() { current = 0; }

PatrolMode::~PatrolMode() = default;

// Emptied in place (the reference count kept).
int PatrolMode::OnShutdown() {
    points.clear();
    current = 0;
    return 0;
}

// A copy with its own points.
PatrolMode* PatrolMode::Clone() {
    auto* clone = new PatrolMode();
    clone->points = points;
    clone->current = current;
    return clone;
}

FearMode* FearMode::Clone() {
    auto* clone = new FearMode(flag);
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
