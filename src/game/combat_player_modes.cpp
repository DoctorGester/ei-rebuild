// The Player and Tamed AI modes' lifetime and constant slots (00551E10-00552180; A-U7). The Tamed
// mode (vtable 0073F0C4) is a Player mode with its own copies of these slots. See unit_ai.h and
// docs/notes/combat.md.
#include "unit_ai.h"

#include "ei/hook.h"
#include "lib/crt.h"

namespace {
constexpr int kTypePlayer = 6;
constexpr int kTypeTamed = 8;
constexpr int kPlayerPriority = 10;
constexpr int kTamedPriority = 100000000;

// A counted copy of a reference (the copy constructor's).
CEngineObject* Share(CEngineObject* object) {
    if (object) ++object->refCount_;
    return object;
}

void ReleaseReferences(PlayerMode* mode) {
    if (mode->leader) mode->leader->Release();
    mode->leader = nullptr;
    if (mode->target) mode->target->Release();
    mode->target = nullptr;
}

// The copy constructor: the base vtable 0073D22C, no references, the fields copied, then `vtable`.
PlayerMode* CloneAs(const PlayerMode* from, void* vtable) {
    auto* mode = static_cast<PlayerMode*>(gamecrt::operator_new(sizeof(PlayerMode)));
    if (!mode) return nullptr;   // the original writes +4 before this test
    mode->refs = 0;
    mode->unknown_06 = 0;
    mode->vtable = aimode::kActionBaseVtable;
    mode->target = Share(from->target);
    mode->leader = Share(from->leader);
    mode->point = from->point;
    mode->state = from->state;
    mode->run = from->run;
    mode->vtable = vtable;
    return mode;
}
} // namespace

int __fastcall aimode::PlayerPriority(AIMode*, void*, AIBrain*) { return kPlayerPriority; }
EI_REPLACE(0x00552090, aimode::PlayerPriority);
int __fastcall aimode::TamedPriority(AIMode*, void*, AIBrain*) { return kTamedPriority; }
EI_REPLACE(0x00552140, aimode::TamedPriority);
int __fastcall aimode::PlayerType(AIMode*) { return kTypePlayer; }
EI_REPLACE(0x005520A0, aimode::PlayerType);
int __fastcall aimode::TamedType(AIMode*) { return kTypeTamed; }
EI_REPLACE(0x00552150, aimode::TamedType);

void PlayerMode::Destruct() {
    vtable = aimode::kPlayerVtable;
    ReleaseReferences(this);
    vtable = aimode::kRootVtable;
}
EI_REPLACE_METHOD(0x005520D0, PlayerMode::Destruct);

void PlayerMode::DestructTamed() {
    vtable = aimode::kPlayerVtable;
    ReleaseReferences(this);
    vtable = aimode::kRootVtable;
}
EI_REPLACE_METHOD(0x00552180, PlayerMode::DestructTamed);

PlayerMode* PlayerMode::DeletingDestruct(int flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005520B0, PlayerMode::DeletingDestruct);

PlayerMode* PlayerMode::DeletingDestructTamed(int flags) {
    DestructTamed();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00552160, PlayerMode::DeletingDestructTamed);

PlayerMode* PlayerMode::Restart() {
    Destruct();
    return Construct();
}
EI_REPLACE_METHOD(0x00552000, PlayerMode::Restart);

PlayerMode* PlayerMode::RestartTamed() {
    Destruct();
    Construct();
    vtable = aimode::kTamedVtable;
    return this;
}
EI_REPLACE_METHOD(0x00551EC0, PlayerMode::RestartTamed);

PlayerMode* PlayerMode::Clone() { return CloneAs(this, aimode::kPlayerVtable); }
EI_REPLACE_METHOD(0x00551F50, PlayerMode::Clone);

PlayerMode* PlayerMode::CloneTamed() { return CloneAs(this, aimode::kTamedVtable); }
EI_REPLACE_METHOD(0x00551E10, PlayerMode::CloneTamed);
