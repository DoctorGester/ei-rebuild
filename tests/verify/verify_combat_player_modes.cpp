// Checks for combat_player_modes.cpp (A-U7): the Player / Tamed modes' slots.
// - Priority and type (slots 4 and 8): both run, EAX compared. Every party unit's Think asks for
//   its Player mode's priority (any scenario with the hero: newgame, combat, movement).
// - Restart (slot 2, run when a shared mode loses its list place, e.g. ClearModes / Reset on a
//   party unit: hero_death), Clone (slot 3, sharing a copy-on-share mode), the destructors (a
//   Player mode deleted: a party unit removed, zone changes): ours only, the state they leave.
// The Tamed copies need a tamed unit (taming, 00551AE0); no scenario tames one yet.
#include "verify.h"

#include "../../src/game/unit_ai.h"

namespace {
void* s_unused;

bool Blank(const PlayerMode* mode) {
    return !mode->target && !mode->leader && mode->state == PlayerMode::kStateFollow && mode->run == 0;
}

bool SameFields(const PlayerMode* a, const PlayerMode* b) {
    return a->target == b->target && a->leader == b->leader && a->point.x == b->point.x && a->point.y == b->point.y &&
           a->point.z == b->point.z && a->state == b->state && a->run == b->run;
}
} // namespace

// MOV EAX,imm32 (5 bytes) each.
static int(__fastcall* s_playerPriority)(AIMode*, void*, AIBrain*);
static int __fastcall PlayerPriorityVerify(AIMode* self, void*, AIBrain* brain) {
    int expected = s_playerPriority(self, nullptr, brain);
    int actual = aimode::PlayerPriority(self, nullptr, brain);
    ei::verify::Check("PlayerMode::Evaluate", expected, actual, "%d");
    return expected;
}
EI_VERIFY(0x00552090, PlayerPriorityVerify, 5, s_playerPriority);

static int(__fastcall* s_tamedPriority)(AIMode*, void*, AIBrain*);
static int __fastcall TamedPriorityVerify(AIMode* self, void*, AIBrain* brain) {
    int expected = s_tamedPriority(self, nullptr, brain);
    int actual = aimode::TamedPriority(self, nullptr, brain);
    ei::verify::Check("TamedMode::Evaluate", expected, actual, "%d");
    return expected;
}
EI_VERIFY(0x00552140, TamedPriorityVerify, 5, s_tamedPriority);

static int(__fastcall* s_playerType)(AIMode*);
static int __fastcall PlayerTypeVerify(AIMode* self) {
    int expected = s_playerType(self);
    ei::verify::Check("PlayerMode::GetType", expected, aimode::PlayerType(self), "%d");
    return expected;
}
EI_VERIFY(0x005520A0, PlayerTypeVerify, 5, s_playerType);

static int(__fastcall* s_tamedType)(AIMode*);
static int __fastcall TamedTypeVerify(AIMode* self) {
    int expected = s_tamedType(self);
    ei::verify::Check("TamedMode::GetType", expected, aimode::TamedType(self), "%d");
    return expected;
}
EI_VERIFY(0x00552150, TamedTypeVerify, 5, s_tamedType);

static void __fastcall DestructVerify(PlayerMode* self) {
    self->Destruct();
    ei::verify::Check("PlayerMode::Destruct", true, self->vtable == aimode::kRootVtable && !self->target && !self->leader, "");
}
EI_VERIFY(0x005520D0, DestructVerify, 0, s_unused);

static void __fastcall DestructTamedVerify(PlayerMode* self) {
    self->DestructTamed();
    ei::verify::Check("TamedMode::Destruct", true, self->vtable == aimode::kRootVtable && !self->target && !self->leader, "");
}
EI_VERIFY(0x00552180, DestructTamedVerify, 0, s_unused);

// The memory may be gone afterwards: counted.
static PlayerMode* __fastcall DeletingDestructVerify(PlayerMode* self, void*, int flags) {
    PlayerMode* result = self->DeletingDestruct(flags);
    ei::verify::Check("PlayerMode::DeletingDestruct", true, result == self, "");
    return result;
}
EI_VERIFY(0x005520B0, DeletingDestructVerify, 0, s_unused);

static PlayerMode* __fastcall DeletingDestructTamedVerify(PlayerMode* self, void*, int flags) {
    PlayerMode* result = self->DeletingDestructTamed(flags);
    ei::verify::Check("TamedMode::DeletingDestruct", true, result == self, "");
    return result;
}
EI_VERIFY(0x00552160, DeletingDestructTamedVerify, 0, s_unused);

static PlayerMode* __fastcall RestartVerify(PlayerMode* self) {
    PlayerMode* result = self->Restart();
    ei::verify::Check("PlayerMode::Restart", true, result == self && self->vtable == aimode::kPlayerVtable && Blank(self), "");
    return result;
}
EI_VERIFY(0x00552000, RestartVerify, 0, s_unused);

static PlayerMode* __fastcall RestartTamedVerify(PlayerMode* self) {
    PlayerMode* result = self->RestartTamed();
    ei::verify::Check("TamedMode::Restart", true, result == self && self->vtable == aimode::kTamedVtable && Blank(self), "");
    return result;
}
EI_VERIFY(0x00551EC0, RestartTamedVerify, 0, s_unused);

static PlayerMode* __fastcall CloneVerify(PlayerMode* self) {
    PlayerMode* copy = self->Clone();
    bool ok = !copy || (copy->vtable == aimode::kPlayerVtable && copy->refs == 0 && SameFields(copy, self));
    ei::verify::Check("PlayerMode::Clone", true, ok, "");
    return copy;
}
EI_VERIFY(0x00551F50, CloneVerify, 0, s_unused);

static PlayerMode* __fastcall CloneTamedVerify(PlayerMode* self) {
    PlayerMode* copy = self->CloneTamed();
    bool ok = !copy || (copy->vtable == aimode::kTamedVtable && copy->refs == 0 && SameFields(copy, self));
    ei::verify::Check("TamedMode::Clone", true, ok, "");
    return copy;
}
EI_VERIFY(0x00551E10, CloneTamedVerify, 0, s_unused);
