// The player and tamed AI modes' lifetimes and own slots: clone, reset,
// destructors, priority and type. The tamed mode is the player mode a taming
// installs (combat_taming.cpp); its slots differ only in their constants.
#include "unit_ai.h"

namespace {
constexpr int kPlayerPriority = 10;
constexpr int kTamedPriority = 100000000;

// The clones' common part: `copy` (null when the allocation failed) given the mode's target and
// leader (one more reference each), point, state and run.
template <typename Mode>
Mode* CopyPlayer(const PlayerMode* self, Mode* copy) {
    if (!copy) return nullptr;
    if (self->target) ++self->target->refCount_;
    copy->target = self->target;
    if (self->leader) ++self->leader->refCount_;
    copy->leader = self->leader;
    copy->point = self->point;
    copy->state = self->state;
    copy->run = self->run;
    return copy;
}
} // namespace

PlayerMode::~PlayerMode() {
    if (leader) leader->Release();
    leader = nullptr;
    if (target) target->Release();
    target = nullptr;
}

TamedMode::~TamedMode() {}

void PlayerMode::Reset() {
    if (leader) leader->Release();
    leader = nullptr;
    if (target) target->Release();
    target = nullptr;
    point = Vector3{0.0f, 0.0f, 0.0f};
    state = kStateFollow;
    run = 0;
}

int PlayerMode::OnShutdown() {
    Reset();
    return 0;
}

int TamedMode::OnShutdown() {
    Reset();
    return 0;
}

PlayerMode* PlayerMode::Clone() { return CopyPlayer(this, new PlayerMode()); }

TamedMode* TamedMode::Clone() { return CopyPlayer(this, new TamedMode()); }

int PlayerMode::Evaluate(AIBrain*) { return kPlayerPriority; }

int TamedMode::Evaluate(AIBrain*) { return kTamedPriority; }

int PlayerMode::GetType() { return kModePlayer; }

int TamedMode::GetType() { return kModeTamed; }
