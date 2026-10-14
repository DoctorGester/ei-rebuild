// Checks for the controller's movement modes, idle and movement animations and paths
// (src/game/combat_motion.cpp).
//
// CanReactFor only reads: compared. BuildIdle and BuildMoveSteps rebuild their output list from
// scratch: ours, the result copied, then the original into the same list; counts, steps (without
// byte 3, which the originals never set) and the extra dword compared. The rest change orders,
// animations, paths and the sector map: ours only, with checks that hold for any correct run.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/combat_plan.h"
#include "combat_verify_util.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;
int UnitId(const CUnitServer* self) { return self->unit ? self->unit->id_ : -1; }
template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

struct StepsCopy {
    int count = 0;
    int extra = 0;
    std::vector<AnimationStep> steps;
    void Take(const AnimationSteps* list) {
        count = list->count;
        extra = list->extra;
        steps.assign(list->steps, list->steps + (count > 0 ? count : 0));
    }
    bool SameAs(const AnimationSteps* list) const {
        if (list->count != count || list->extra != extra) return false;
        for (int i = 0; i < count; ++i) {
            const AnimationStep& a = steps[static_cast<std::size_t>(i)];
            const AnimationStep& b = list->steps[i];
            if (a.animation != b.animation || a.variant != b.variant || a.flags != b.flags ||
                a.duration != b.duration || a.kind != b.kind)
                return false;
        }
        return true;
    }
};
} // namespace

// 00549D00: PUSH ESI; MOV ESI,ECX; CMP DWORD PTR [ESI+0x190],3 (10 bytes). Results compared.
using ReactFn = bool(__fastcall*)(CUnitServer*, void*, int);
static ReactFn s_react;
static bool __fastcall CanReactForVerify(CUnitServer* self, void*, int ticks) {
    bool ours = self->CanReactFor(ticks);
    bool theirs = s_react(self, nullptr, ticks);
    ei::verify::Check("CUnitServer::CanReactFor", theirs, ours, "unit %d, ticks %d", UnitId(self), ticks);
    return theirs;
}
EI_VERIFY(0x00549D00, CanReactForVerify, 10, s_react);

// 00548DE0 / 00548FA0: ours only. The state and a started queue.
static void __fastcall PlayAnimationOrderVerify(CUnitServer* self, void*, AnimationSteps steps) {
    self->PlayAnimationOrder(steps);
    ei::verify::Check("CUnitServer::PlayAnimationOrder", true, self->state == 3 && At<int>(self, 0x38) != -1, "unit %d",
                      UnitId(self));
}
EI_VERIFY(0x00548DE0, PlayAnimationOrderVerify, 0, s_unused);

static void __fastcall PlayEmoteOrderVerify(CUnitServer* self, void*, AnimationSteps steps) {
    self->PlayEmoteOrder(steps);
    ei::verify::Check("CUnitServer::PlayEmoteOrder", true, self->state == 4 && At<int>(self, 0x38) != -1, "unit %d",
                      UnitId(self));
}
EI_VERIFY(0x00548FA0, PlayEmoteOrderVerify, 0, s_unused);

// 00550D90: SUB ESP,0xC; PUSH EBX; PUSH EBP (5 bytes). The list compared.
using BuildIdleFn = void(__fastcall*)(CUnitServer*, void*, AnimationSteps*);
static BuildIdleFn s_buildIdle;
static void __fastcall BuildIdleVerify(CUnitServer* self, void*, AnimationSteps* steps) {
    // The animation lookup (005301B0 -> 00519380) may pick an entry with rand(): both runs start
    // from the same generator state, which is compared too.
    combatverify::RandomState before, mine, theirs;
    before.Save();
    self->BuildIdle(steps);
    mine.Save();
    StepsCopy ours;
    ours.Take(steps);
    before.Restore();
    s_buildIdle(self, nullptr, steps);
    theirs.Save();
    ei::verify::Check("CUnitServer::BuildIdle", true, ours.SameAs(steps) && mine.SameAs(theirs),
                      "unit %d, %d steps (original %d)", UnitId(self), ours.count, steps->count);
}
EI_VERIFY(0x00550D90, BuildIdleVerify, 5, s_buildIdle);

// 00550F30: ours only. The idle list, under the key the unit has now.
static AnimationSteps* __fastcall IdleAnimationVerify(CUnitServer* self) {
    AnimationSteps* result = self->IdleAnimation();
    std::uint32_t key = self->WeaponTypeMask() | self->MoveModeMask(&self->moveMode);
    ei::verify::Check("CUnitServer::IdleAnimation", true,
                      result == &At<AnimationSteps>(self, 0x960) && At<std::uint32_t>(self, 0x95C) == key, "unit %d",
                      UnitId(self));
    return result;
}
EI_VERIFY(0x00550F30, IdleAnimationVerify, 0, s_unused);

// 005474B0: ours only (counted).
static void __fastcall PlayIdleVerify(CUnitServer* self) {
    self->PlayIdle();
    ei::verify::Check("CUnitServer::PlayIdle", true, true, "");
}
EI_VERIFY(0x005474B0, PlayIdleVerify, 0, s_unused);

// 005511A0: SUB ESP,0xC; PUSH EBX; PUSH EBP (5 bytes). The list compared.
using BuildMoveFn = void(__fastcall*)(CUnitServer*, void*, AnimationSteps*, int);
static BuildMoveFn s_buildMove;
static void __fastcall BuildMoveStepsVerify(CUnitServer* self, void*, AnimationSteps* steps, int once) {
    combatverify::RandomState before, mine, theirs;   // as BuildIdle: the lookup may draw rand()
    before.Save();
    self->BuildMoveSteps(steps, static_cast<char>(once));
    mine.Save();
    StepsCopy ours;
    ours.Take(steps);
    before.Restore();
    s_buildMove(self, nullptr, steps, once);
    theirs.Save();
    ei::verify::Check("CUnitServer::BuildMoveSteps", true, ours.SameAs(steps) && mine.SameAs(theirs),
                      "unit %d, once %d, %d steps (original %d)", UnitId(self), once & 0xFF, ours.count, steps->count);
}
EI_VERIFY(0x005511A0, BuildMoveStepsVerify, 5, s_buildMove);

// 00551400: ours only. The movement list.
static AnimationSteps* __fastcall NextAnimationVerify(CUnitServer* self) {
    AnimationSteps* result = self->NextAnimation();
    ei::verify::Check("CUnitServer::NextAnimation", true, result == &At<AnimationSteps>(self, 0x944), "unit %d",
                      UnitId(self));
    return result;
}
EI_VERIFY(0x00551400, NextAnimationVerify, 0, s_unused);

// 005494D0: ours only. The unit acts afterwards.
static void __fastcall StartActionVerify(CUnitServer* self, void*, char follow, char flags) {
    self->StartAction(follow, flags);
    ei::verify::Check("CUnitServer::StartAction", 5, self->state, "unit %d", UnitId(self));
}
EI_VERIFY(0x005494D0, StartActionVerify, 0, s_unused);

// 00548520: ours only (counted).
static void __fastcall UpdateSneakVerify(CUnitServer* self) {
    self->UpdateSneak();
    ei::verify::Check("CUnitServer::UpdateSneak", true, true, "");
}
EI_VERIFY(0x00548520, UpdateSneakVerify, 0, s_unused);

// 0054A150: ours only. A change leaves both records' modes and flags equal.
static bool __fastcall ChangeMoveModeVerify(CUnitServer* self) {
    bool result = self->ChangeMoveMode();
    bool ok = !result || (self->moveMode.mode == self->moveMode2.mode && self->moveMode.flag4 == self->moveMode2.flag4);
    ei::verify::Check("CUnitServer::ChangeMoveMode", true, ok, "unit %d, result %d", UnitId(self), result);
    return result;
}
EI_VERIFY(0x0054A150, ChangeMoveModeVerify, 0, s_unused);

// 00548700: ours only. A path leaves the unit idle (state 0), none in state 6.
static bool __fastcall PathToVerify(CUnitServer* self, void*, const float* point, float range) {
    bool result = self->PathTo(point, range);
    ei::verify::Check("CUnitServer::PathTo", result ? 0 : 6, self->state, "unit %d, result %d", UnitId(self), result);
    return result;
}
EI_VERIFY(0x00548700, PathToVerify, 0, s_unused);

// 005468D0: ours only. The list is well formed: its count is the number of nodes, the last of
// which is the tail, and every node has a unit. (Whether a unit really left the sector map
// depends on CSectorMap::Remove, which may refuse.)
static void __fastcall LiftNearbyVerify(CUnitServer* self, void*, void* list, const float* point, char moving) {
    self->LiftNearby(list, point, moving);
    struct Node {
        Node* next;
        Node* prev;
        CUnit* data;
    };
    bool ok = true;
    int count = 0;
    Node* last = nullptr;
    for (Node* node = At<Node*>(list, 4); node && count < 1000; node = node->next, ++count) {
        if (!node->data || node->prev != last) ok = false;
        last = node;
    }
    ok = ok && count == At<int>(list, 0xC) && last == At<Node*>(list, 8);
    ei::verify::Check("CUnitServer::LiftNearby", true, ok, "unit %d, %d listed", UnitId(self), count);
}
EI_VERIFY(0x005468D0, LiftNearbyVerify, 0, s_unused);

// 0054EB30: ours only (counted).
static void __fastcall ReportPathVerify(CUnitServer* self, void*, void* path, char partial) {
    self->ReportPath(path, partial);
    ei::verify::Check("CUnitServer::ReportPath", true, true, "");
}
EI_VERIFY(0x0054EB30, ReportPathVerify, 0, s_unused);

// 00546480: ours only. The reset state.
static void __fastcall ResetControllerVerify(CUnitServer* self) {
    self->ResetController();
    bool ok = self->state == 6 && self->action.kind == 0 && self->action2.kind == 9 && !self->action.target &&
              At<std::uint32_t>(self, 0x940) == 0x666DEAD && At<std::uint32_t>(self, 0x95C) == 0x666DEAD &&
              self->moveMode.mode == 2 && self->moveMode2.mode == 2 && At<void*>(self, 0x920) == nullptr;
    ei::verify::Check("CUnitServer::ResetController", true, ok, "unit %d", UnitId(self));
}
EI_VERIFY(0x00546480, ResetControllerVerify, 0, s_unused);

// 00554450: ours only. The current order is empty and the records follow the unit's modes.
static void __fastcall ResyncWithUnitVerify(CUnitServer* self) {
    self->ResyncWithUnit();
    auto modeOf = [](const MoveModeState& record) { return record.wasMode3 ? 3 : record.mode; };
    bool ok = self->action.kind == 9 && !self->action.target && modeOf(self->moveMode) == self->unit->moveMode &&
              modeOf(self->moveMode2) == self->unit->moveMode2 && (self->state == 2 || self->state == 6);
    ei::verify::Check("CUnitServer::ResyncWithUnit", true, ok, "unit %d, state %d", UnitId(self), self->state);
}
EI_VERIFY(0x00554450, ResyncWithUnitVerify, 0, s_unused);
