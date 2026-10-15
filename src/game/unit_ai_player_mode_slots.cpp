// The player and tamed AI modes' own vtable slots (00551E10-00552180, A-U6): clone, reset,
// destructors, priority and type. The tamed mode (vtable 0073F0C4) is the player mode a taming
// installs (combat_taming.cpp); its slots differ only in the vtable they leave and their constants.
#include "unit_ai.h"

#include "ei/hook.h"
#include "lib/crt.h"

namespace {
constexpr int kPlayerPriority = 10;
constexpr int kTamedPriority = 100000000;

// The clones' common part: the abstract action base's vtable while the members are copied.
PlayerMode* CloneWith(const PlayerMode* self, void* vtable) {
    auto* copy = static_cast<PlayerMode*>(gamecrt::operator_new(sizeof(PlayerMode)));
    copy->refs = 0;   // written before the test, as the original does
    copy->refsHigh = 0;
    if (!copy) return nullptr;
    copy->vtable = aimode::kActionBaseVtable;
    copy->target = nullptr;
    if (self->target) {
        ++self->target->refCount_;
        copy->target = self->target;
    }
    copy->leader = nullptr;
    if (self->leader) {
        ++self->leader->refCount_;
        copy->leader = self->leader;
    }
    copy->point = self->point;
    copy->state = self->state;
    copy->run = self->run;
    copy->vtable = vtable;
    return copy;
}
} // namespace

PlayerMode* PlayerMode::ClonePlayer() { return CloneWith(this, aimode::kPlayerVtable); }
EI_REPLACE_METHOD(0x00551F50, PlayerMode::ClonePlayer);

PlayerMode* PlayerMode::CloneTamed() { return CloneWith(this, aimode::kTamedVtable); }
EI_REPLACE_METHOD(0x00551E10, PlayerMode::CloneTamed);

void PlayerMode::Destruct() {
    vtable = aimode::kPlayerVtable;
    if (leader) leader->Release();
    leader = nullptr;
    if (target) target->Release();
    target = nullptr;
    vtable = aimode::kRootVtable;
}
EI_REPLACE_METHOD(0x005520D0, PlayerMode::Destruct);
EI_REPLACE_METHOD(0x00552180, PlayerMode::Destruct);

PlayerMode* PlayerMode::DeletingDestruct(int flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005520B0, PlayerMode::DeletingDestruct);
EI_REPLACE_METHOD(0x00552160, PlayerMode::DeletingDestruct);

PlayerMode* PlayerMode::ResetPlayer() {
    Destruct();
    return Construct();
}
EI_REPLACE_METHOD(0x00552000, PlayerMode::ResetPlayer);

PlayerMode* PlayerMode::ResetTamed() {
    Destruct();
    Construct();
    vtable = aimode::kTamedVtable;
    return this;
}
EI_REPLACE_METHOD(0x00551EC0, PlayerMode::ResetTamed);

int AIMode::PlayerPriority(AIBrain*) { return kPlayerPriority; }
EI_REPLACE_METHOD(0x00552090, AIMode::PlayerPriority);

int AIMode::TamedPriority(AIBrain*) { return kTamedPriority; }
EI_REPLACE_METHOD(0x00552140, AIMode::TamedPriority);

int __fastcall aimode::PlayerType(AIMode*) { return kModePlayer; }
EI_REPLACE(0x005520A0, aimode::PlayerType);

int __fastcall aimode::TamedType(AIMode*) { return kModeTamed; }
EI_REPLACE(0x00552150, aimode::TamedType);
