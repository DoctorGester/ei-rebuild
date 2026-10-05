// Combat, ninth part: taming (the skill 0054F000 and a spell call it). A tame attempt that beats
// the roll moves the creature a stage along: 1 no longer hostile to the tamer's side, 2 on the
// tamer's side (its own side kept at +0x90C), 3 in the tamer's party with a "tamed" AI mode; a
// failed attempt (or one on a creature with a player) undoes the taming when it was the tamer's
// side doing it, and counts as an attack. See docs/notes/combat.md ("Fourth round").
#include "combat.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_engine.h"

#include <cstdint>
#include "originals_library.h"
#include "originals_units.h"

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

constexpr int kOrderNone = 9;
constexpr int kSlotRelease = 4;
constexpr int kSlotTameLevel = 0x9C / 4;   // the controller's virtual slot 39 (int)
constexpr int kStatTaming = 2;
constexpr int kModeTamed = 8;              // the brain mode removed when the taming is undone

// Forwarders (thiscall as __fastcall with the unused EDX).
// 00525E90 UnitStats: a skill's value (combat_actions.cpp uses it too).
__declspec(naked) double __fastcall StatsSkillValue(UnitStats*, void*, int, CUnit*) EI_ORIGINAL(0x00525E90)
// 00455680 CDiplomacy::OnAttack (object_diplomacy.cpp; world + 0x12C64).
// CUnit (unit.cpp): 00530120 SetSide, 00530180 SetRelationsB, 005300D0 SetPlayer.
__declspec(naked) void __fastcall UnitSetSide(CUnit*, void*, int) EI_ORIGINAL(0x00530120)
__declspec(naked) void __fastcall UnitSetRelationsB(CUnit*, void*, std::uint32_t) EI_ORIGINAL(0x00530180)
inline void __fastcall UnitSetPlayer(CUnit* a0, void*, char* a2) { UnitSetControllerOriginal((void*)a0, nullptr, (void*)a2); }
// 00665D20 the player: prepares to take a unit in (no arguments).
__declspec(naked) void __fastcall PlayerTakeUnit(char*) EI_ORIGINAL(0x00665D20)
// The brain (unit_ai.cpp, controller + 0x980): 005D3760 the player mode's constructor (0x24
// bytes), 005D5F60 AddMode, 005D6140 RemoveMode(kind), 005D7790 StopAction.
__declspec(naked) void* __fastcall ModePlayerConstruct(void*) EI_ORIGINAL(0x005D3760)
__declspec(naked) void __fastcall BrainAddMode(void*, void*, void*) EI_ORIGINAL(0x005D5F60)
__declspec(naked) void __fastcall BrainRemoveMode(void*, void*, int) EI_ORIGINAL(0x005D6140)
__declspec(naked) void __fastcall BrainStopAction(void*) EI_ORIGINAL(0x005D7790)
// 0052B030 a reference's release; MFC CPlex::FreeDataChain.
__declspec(naked) void __fastcall ReferenceRelease(CEngineObject**) EI_ORIGINAL(0x0052B030)
inline void __fastcall PlexFreeDataChain(void* p) { PlexFreeDataChainOriginal(p); }
// This file's functions through their game addresses.
__declspec(naked) void __fastcall CtlUndoTaming(CUnitServer*) EI_ORIGINAL(0x00552280)

int& Stage(CUnitServer* self) { return At<int>(self, 0x908); }

// The side that tamed the unit (-1 when not tamed) is the attacker's: the taming is undone.
void UndoIfTamedBy(CUnitServer* self, CUnit* tamer) {
    int side = Stage(self) == 0 ? -1 : At<int>(self, 0x910);
    if (side == tamer->side) CtlUndoTaming(self);
}

// An empty reference list of the brain (CList of one reference per node): each released, then
// RemoveAll. `release` picks how the original releases the element.
template <bool kViaRelease>
void ClearReferenceList(CUnitServer* self, int head) {
    struct Node {
        Node* next;
        Node* prev;
        CEngineObject* item;
    };
    for (Node* node = At<Node*>(self, head); node; node = node->next) {
        if (kViaRelease) {
            ReferenceRelease(&node->item);
        } else {
            if (node->item) ei::VCall<void>(node->item, kSlotRelease);
            node->item = nullptr;
        }
    }
    At<int>(self, head + 8) = 0;    // count
    At<int>(self, head + 0xC) = 0;  // free list
    At<int>(self, head + 4) = 0;    // tail
    At<int>(self, head) = 0;        // head
    PlexFreeDataChain(At<void*>(self, head + 0x10));
    At<int>(self, head + 0x10) = 0;
}

} // namespace

BOOL CUnitServer::TryTame(CUnit* tamer, float roll) {
    float bonus = static_cast<float>(StatsSkillValue(&stats, nullptr, kStatTaming, unit));
    int level = ei::VCall<int>(this, kSlotTameLevel);
    float limit = static_cast<float>(level * 10) + bonus;
    if (unit->player || limit >= roll) {   // FCOMP: C0 (limit below the roll, or unordered) succeeds
        // A unit with a player cannot be tamed; a roll not above the limit fails. Either way an attack.
        UndoIfTamedBy(this, tamer);
        DiplomacyOnAttack(static_cast<char*>(ManagerContextOf(this)) + 0x12C64, nullptr, tamer, unit, 10);
        return 0;
    }
    char* tamerPlayer = tamer->player;
    switch (Stage(this)) {
    case 0:
        UnitSetRelationsB(unit, nullptr, ~(1u << (tamer->side & 31)));
        Stage(this) = 1;
        break;
    case 1:
        At<int>(this, 0x90C) = unit->side;
        UnitSetSide(unit, nullptr, tamer->side);
        Stage(this) = 2;
        break;
    case 2: {
        if (!tamerPlayer) break;
        PlayerTakeUnit(tamerPlayer);
        UnitSetSide(unit, nullptr, tamer->side);
        UnitSetPlayer(unit, nullptr, tamerPlayer);
        auto* mode = static_cast<std::uint8_t*>(gamecrt::operator_new(0x24));
        At<int>(mode, 4) = 0;   // written before the test, as the original does
        if (mode) {
            ModePlayerConstruct(mode);
            At<void*>(mode, 0) = reinterpret_cast<void*>(0x0073F0C4);
        }
        BrainAddMode(brain, nullptr, mode);
        ClearReferenceList<false>(this, 0xAB0);
        ClearReferenceList<true>(this, 0xACC);
        Stage(this) = 3;
        break;
    }
    default:
        break;
    }
    At<int>(this, 0x910) = tamer->side;
    BrainStopAction(brain);
    if (action.kind != 0 && action2.kind != 7 && action2.kind != 8) {
        action2.kind = kOrderNone;
        ReferenceRelease(&action2.target);
        action2.flag18 = 0;
        action2.unknown_1C = 0;
        action2.unknown_20 = 0;
        action2.kind = 0;
        if (action2.target) {
            ReferenceRelease(&action2.target);
            action2.target = nullptr;
        }
        action2.unknown_20 = 0;
    }
    return 1;
}
EI_REPLACE_METHOD(0x00551AE0, CUnitServer::TryTame);

void CUnitServer::UndoTaming() {
    if (Stage(this) == 0) return;
    BrainRemoveMode(brain, nullptr, kModeTamed);
    UnitSetPlayer(unit, nullptr, nullptr);
    UnitSetRelationsB(unit, nullptr, 0xFFFFFFFFu);
    if (Stage(this) > 1) UnitSetSide(unit, nullptr, At<int>(this, 0x90C));
    Stage(this) = 0;
}
EI_REPLACE_METHOD(0x00552280, CUnitServer::UndoTaming);
