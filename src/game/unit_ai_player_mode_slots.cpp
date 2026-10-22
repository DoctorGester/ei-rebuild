// The player and tamed AI modes' lifetimes and own slots (00551E10-00552180, A-U6): clone, reset,
// destructors, priority and type. The tamed mode (vtable 0073F0C4) is the player mode a taming
// installs (combat_taming.cpp); its slots differ only in their constants.
#include "unit_ai.h"

#include "ei/hook.h"

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
EI_REWRITTEN_SPECIAL(0x005520D0, PlayerMode::~PlayerMode);
EI_REWRITTEN_SPECIAL(0x005520B0, PlayerMode::~PlayerMode);

TamedMode::~TamedMode() {}
EI_REWRITTEN_SPECIAL(0x00552180, TamedMode::~TamedMode);
EI_REWRITTEN_SPECIAL(0x00552160, TamedMode::~TamedMode);

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
    return static_cast<int>(reinterpret_cast<std::uintptr_t>(this));
}
EI_REWRITTEN(0x00552000, PlayerMode::OnShutdown);

int TamedMode::OnShutdown() {
    Reset();
    return static_cast<int>(reinterpret_cast<std::uintptr_t>(this));
}
EI_REWRITTEN(0x00551EC0, TamedMode::OnShutdown);

PlayerMode* PlayerMode::Clone() { return CopyPlayer(this, new PlayerMode()); }
EI_REWRITTEN(0x00551F50, PlayerMode::Clone);

TamedMode* TamedMode::Clone() { return CopyPlayer(this, new TamedMode()); }
EI_REWRITTEN(0x00551E10, TamedMode::Clone);

int PlayerMode::Evaluate(AIBrain*) { return kPlayerPriority; }
EI_REWRITTEN(0x00552090, PlayerMode::Evaluate);

int TamedMode::Evaluate(AIBrain*) { return kTamedPriority; }
EI_REWRITTEN(0x00552140, TamedMode::Evaluate);

int PlayerMode::GetType() { return kModePlayer; }
EI_REWRITTEN(0x005520A0, PlayerMode::GetType);

int TamedMode::GetType() { return kModeTamed; }
EI_REWRITTEN(0x00552150, TamedMode::GetType);
