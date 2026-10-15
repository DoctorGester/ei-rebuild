// Checks for the Player mode's fighting and party code (src/game/unit_ai_player.cpp). Reached by
// party units every AI tick (PlayerMode::Execute): any scenario after `newgame` with the hero's
// party (unit_ai, ui_order_move, combat; StaysWithParty while walking in state 2).
#include "verify.h"

#include "../../src/game/unit_ai.h"

#include <vector>

namespace {
void* s_unused;

void InitList(AIObjectList& list) {
    list = {};
    list.vtable = mfc::PtrListVtable();
    list.blockSize = 10;
}

std::vector<CEngineObject*> Elements(const AIObjectList& list) {
    std::vector<CEngineObject*> out;
    for (auto* node = list.head; node; node = node->next) out.push_back(node->data);
    return out;
}
} // namespace

// 005D4860 PlayerMode::StaysWithParty: PUSH -1; PUSH imm32 (7 bytes). Pure: AL compared.
static std::uint32_t(__fastcall* s_staysWithParty)(PlayerMode*, void*, AIBrain*, int);
static std::uint32_t __fastcall StaysWithPartyVerify(PlayerMode* self, void*, AIBrain* brain, int moving) {
    std::uint32_t expected = s_staysWithParty(self, nullptr, brain, moving);
    bool actual = self->StaysWithParty(brain, static_cast<std::uint8_t>(moving) != 0);
    ei::verify::Check("PlayerMode::StaysWithParty", static_cast<int>(expected & 0xFF), actual ? 1 : 0, "mode %p moving %d",
                      self, moving & 0xFF);
    return expected;
}
EI_VERIFY(0x005D4860, StaysWithPartyVerify, 7, s_staysWithParty);

// 005D4600 PlayerMode::CollectParty: MOV EAX,FS:[0] (6 bytes). Output: the original fills a scratch
// list, ours the real one; the element sequences compared.
static void(__fastcall* s_collectParty)(PlayerMode*, void*, AIBrain*, AIObjectList*);
static void __fastcall CollectPartyVerify(PlayerMode* self, void*, AIBrain* brain, AIObjectList* party) {
    AIObjectList theirs;
    InitList(theirs);
    s_collectParty(self, nullptr, brain, &theirs);
    std::vector<CEngineObject*> expected = Elements(theirs);
    theirs.RemoveAll();
    self->CollectParty(brain, party);
    ei::verify::Check("PlayerMode::CollectParty", true, expected == Elements(*party), "mode %p: %d / %d units", self,
                      static_cast<int>(expected.size()), party->count);
}
EI_VERIFY(0x005D4600, CollectPartyVerify, 6, s_collectParty);

// 005D3F50 PlayerMode::AddPartyTargets: PUSH -1; PUSH imm32 (7 bytes). Output: the original appends
// to a scratch copy of the list, ours to the real one; the element sequences compared.
static void(__fastcall* s_addPartyTargets)(PlayerMode*, void*, AIBrain*, AIObjectList*);
static void __fastcall AddPartyTargetsVerify(PlayerMode* self, void*, AIBrain* brain, AIObjectList* targets) {
    AIObjectList theirs;
    InitList(theirs);
    for (auto* node = targets->head; node; node = node->next) theirs.AddTail(node->data);
    s_addPartyTargets(self, nullptr, brain, &theirs);
    std::vector<CEngineObject*> expected = Elements(theirs);
    theirs.RemoveAll();
    self->AddPartyTargets(brain, targets);
    ei::verify::Check("PlayerMode::AddPartyTargets", true, expected == Elements(*targets), "mode %p: %d / %d", self,
                      static_cast<int>(expected.size()), targets->count);
}
EI_VERIFY(0x005D3F50, AddPartyTargetsVerify, 7, s_addPartyTargets);

// 005D4170 PlayerMode::UpdateCombat: ours only. The state stays valid, and an attack it orders has a
// target. Execute calls it for an attack whose target is gone (action 3, brain target cleared when the
// target left the server, t2b_attack_removed); finding nobody, it leaves that state as it was, as the
// original does (T2b), so the attack check only applies when the call changed the action or target.
static void __fastcall UpdateCombatVerify(PlayerMode* self, void*, AIBrain* brain) {
    int action = brain->action;
    CEngineObject* target = brain->target;
    self->UpdateCombat(brain);
    bool changed = brain->action != action || brain->target != target;
    bool ok = self->state >= PlayerMode::kStateFollow && self->state <= PlayerMode::kStateMoveTo &&
              (!changed || brain->action != AIBrain::kActionAttack || brain->target != nullptr);
    ei::verify::Check("PlayerMode::UpdateCombat", true, ok, "mode %p state %d action %d (was %d, target %p was %p)", self,
                      self->state, brain->action, action, brain->target, target);
}
EI_VERIFY(0x005D4170, UpdateCombatVerify, 0, s_unused);
