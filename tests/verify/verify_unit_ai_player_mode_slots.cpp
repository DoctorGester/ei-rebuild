// Checks for the player and tamed modes' own slots (src/game/unit_ai_player_mode_slots.cpp).
// Priorities and types are pure (both run). Clones, resets and destructors take or release
// references: ours only, then the result is checked against the source / a fresh mode. Every
// scenario with a party reaches the player mode's priority and type each tick; clones, resets and
// destructors run when a mode is shared or a party unit's brain goes (zone change, quit); the
// tamed slots need a taming (no scenario).
#include "verify.h"

#include "../../src/game/unit_ai.h"

namespace {
void* s_unused;
using PriorityFn = int(__fastcall*)(AIMode*, void*, AIBrain*);
using TypeFn = int(__fastcall*)(AIMode*);
PriorityFn s_playerPriority, s_tamedPriority;
TypeFn s_playerType, s_tamedType;

bool SameCopy(const PlayerMode* copy, const PlayerMode* source, void* vtable) {
    return copy && copy->vtable == vtable && copy->refs == 0 && copy->target == source->target &&
           copy->leader == source->leader && copy->point.x == source->point.x && copy->point.y == source->point.y &&
           copy->point.z == source->point.z && copy->state == source->state && copy->run == source->run;
}

bool Fresh(const PlayerMode* mode, void* vtable) {
    return mode->vtable == vtable && !mode->target && !mode->leader && mode->state == PlayerMode::kStateFollow &&
           mode->run == 0 && mode->point.x == 0.0f && mode->point.y == 0.0f && mode->point.z == 0.0f;
}
} // namespace

// 00552090 / 00552140: MOV EAX,imm32 (5 bytes).
static int __fastcall PlayerPriorityVerify(AIMode* self, void*, AIBrain* brain) {
    int expected = s_playerPriority(self, nullptr, brain);
    ei::verify::Check("aimode::PlayerPriority", expected, aimode::PlayerPriority(self, nullptr, brain), "mode %p", self);
    return expected;
}
EI_VERIFY(0x00552090, PlayerPriorityVerify, 5, s_playerPriority);

static int __fastcall TamedPriorityVerify(AIMode* self, void*, AIBrain* brain) {
    int expected = s_tamedPriority(self, nullptr, brain);
    ei::verify::Check("aimode::TamedPriority", expected, aimode::TamedPriority(self, nullptr, brain), "mode %p", self);
    return expected;
}
EI_VERIFY(0x00552140, TamedPriorityVerify, 5, s_tamedPriority);

// 005520A0 / 00552150: MOV EAX,imm32 (5 bytes).
static int __fastcall PlayerTypeVerify(AIMode* self) {
    int expected = s_playerType(self);
    ei::verify::Check("aimode::PlayerType", expected, aimode::PlayerType(self), "mode %p", self);
    return expected;
}
EI_VERIFY(0x005520A0, PlayerTypeVerify, 5, s_playerType);

static int __fastcall TamedTypeVerify(AIMode* self) {
    int expected = s_tamedType(self);
    ei::verify::Check("aimode::TamedType", expected, aimode::TamedType(self), "mode %p", self);
    return expected;
}
EI_VERIFY(0x00552150, TamedTypeVerify, 5, s_tamedType);

// 00551F50 / 00551E10: ours only; the copy matches the source.
static PlayerMode* __fastcall ClonePlayerVerify(PlayerMode* self) {
    PlayerMode* copy = self->ClonePlayer();
    ei::verify::Check("PlayerMode::ClonePlayer", true, SameCopy(copy, self, aimode::kPlayerVtable), "mode %p", self);
    return copy;
}
EI_VERIFY(0x00551F50, ClonePlayerVerify, 0, s_unused);

static PlayerMode* __fastcall CloneTamedVerify(PlayerMode* self) {
    PlayerMode* copy = self->CloneTamed();
    ei::verify::Check("PlayerMode::CloneTamed", true, SameCopy(copy, self, aimode::kTamedVtable), "mode %p", self);
    return copy;
}
EI_VERIFY(0x00551E10, CloneTamedVerify, 0, s_unused);

// 00552000 / 00551EC0: ours only; a fresh mode with its vtable, `this` returned.
static PlayerMode* __fastcall ResetPlayerVerify(PlayerMode* self) {
    PlayerMode* result = self->ResetPlayer();
    ei::verify::Check("PlayerMode::ResetPlayer", true, result == self && Fresh(self, aimode::kPlayerVtable), "mode %p", self);
    return result;
}
EI_VERIFY(0x00552000, ResetPlayerVerify, 0, s_unused);

static PlayerMode* __fastcall ResetTamedVerify(PlayerMode* self) {
    PlayerMode* result = self->ResetTamed();
    ei::verify::Check("PlayerMode::ResetTamed", true, result == self && Fresh(self, aimode::kTamedVtable), "mode %p", self);
    return result;
}
EI_VERIFY(0x00551EC0, ResetTamedVerify, 0, s_unused);

// 005520D0 / 00552180: ours only; no references left, root vtable.
static void __fastcall DestructVerify(PlayerMode* self) {
    self->Destruct();
    ei::verify::Check("PlayerMode::Destruct", true, self->vtable == aimode::kRootVtable && !self->target && !self->leader,
                      "mode %p", self);
}
EI_VERIFY(0x005520D0, DestructVerify, 0, s_unused);
EI_VERIFY(0x00552180, DestructVerify, 0, s_unused);

// 005520B0 / 00552160: ours only; `this` returned (the object may be gone).
static PlayerMode* __fastcall DeletingDestructVerify(PlayerMode* self, void*, int flags) {
    PlayerMode* result = self->DeletingDestruct(flags);
    ei::verify::Check("PlayerMode::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "flags %d", flags);
    return result;
}
EI_VERIFY(0x005520B0, DeletingDestructVerify, 0, s_unused);
EI_VERIFY(0x00552160, DeletingDestructVerify, 0, s_unused);
