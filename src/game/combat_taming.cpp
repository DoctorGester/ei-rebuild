// Combat, ninth part: taming (the skill 0054F000 and a spell call it). A tame attempt that beats
// the roll moves the creature a stage along: 1 no longer hostile to the tamer's side, 2 on the
// tamer's side (its own side kept at +0x90C), 3 in the tamer's party with a "tamed" AI mode; a
// failed attempt (or one on a creature with a player) undoes the taming when it was the tamer's
// side doing it, and counts as an attack. See docs/notes/combat.md ("Order lifecycle").
#include "combat.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "player_messages.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"   // SetVtable
#include "originals_engine.h"

#include <cstdint>
#include "originals_library.h"
#include "originals_units.h"
#include "originals_ai.h"

namespace {

constexpr int kOrderNone = 9;
constexpr int kStatTaming = 2;

// Forwarders (thiscall as __fastcall with the unused EDX).
// 00455680 CDiplomacy::OnAttack (object_diplomacy.cpp; world + 0x12C64).
// CUnit (unit.cpp): 00530120 SetSide, 00530180 SetRelationsB, 005300D0 SetPlayer.
inline void __fastcall UnitSetPlayer(CUnit* a0, void*, char* a2) { static_cast<CUnit*>(a0)->SetPlayer(a2); }
// The brain (unit_ai.cpp, controller + 0x980): 005D3760 the player mode's constructor (0x24
// bytes), 005D5F60 AddMode, 005D6140 RemoveMode(kind), 005D7790 StopAction.
void* __fastcall ModePlayerConstruct(void* a0) { return static_cast<PlayerMode*>(a0)->Construct(); }
void __fastcall BrainRemoveMode(void* a0, void*, int a2) { static_cast<AIBrain*>(a0)->RemoveModesOfType(a2); }
// 0052B030 a reference's release; MFC CPlex::FreeDataChain.
void __fastcall ReferenceRelease(CEngineObject** a0) { CountedRefResetOriginal((void*)a0); }
inline void __fastcall PlexFreeDataChain(void* p) { PlexFreeDataChainOriginal(p); }

int& Stage(CUnitServer* self) { return self->tameStage; }

// The side that tamed the unit (-1 when not tamed) is the attacker's: the taming is undone.
void UndoIfTamedBy(CUnitServer* self, CUnit* tamer) {
    int side = Stage(self) == 0 ? -1 : self->tamerSide;
    if (side == tamer->side) self->UndoTaming();
}

// An empty reference list of the brain (CList of one reference per node): each released, then
// RemoveAll. `release` picks how the original releases the element.
template <bool kViaRelease>
void ClearReferenceList(mfc::CTypedPtrList<CEngineObject>& list) {
    for (auto* node = list.head; node; node = node->next) {
        if (kViaRelease) {
            ReferenceRelease(&node->data);
        } else {
            if (node->data) node->data->Release();
            node->data = nullptr;
        }
    }
    list.count = 0;
    list.free = nullptr;
    list.tail = nullptr;
    list.head = nullptr;
    PlexFreeDataChain(list.blocks);
    list.blocks = nullptr;
}

} // namespace

BOOL CUnitServer::TryTame(CUnit* tamer, float roll) {
    float bonus = static_cast<float>(stats.AttributeWithItems(kStatTaming, unit));
    int level = ServerSlots(this)->TameStage();   // slot 39
    float limit = static_cast<float>(level * 10) + bonus;
    if (unit->player || limit >= roll) {   // FCOMP: C0 (limit below the roll, or unordered) succeeds
        // A unit with a player cannot be tamed; a roll not above the limit fails. Either way an attack.
        UndoIfTamedBy(this, tamer);
        static_cast<CDiplomacy*>(&static_cast<CWorldServer*>(ManagerContextOf(this))->diplomacy_)->OnAttack(tamer, unit, 10);
        return 0;
    }
    char* tamerPlayer = tamer->player;
    switch (Stage(this)) {
    case 0:
        unit->SetRelationsB(~(1u << (tamer->side & 31)));
        Stage(this) = 1;
        break;
    case 1:
        ownSide = unit->side;
        unit->SetSide(tamer->side);
        Stage(this) = 2;
        break;
    case 2: {
        if (!tamerPlayer) break;
        reinterpret_cast<CPlayer*>(tamerPlayer)->ReleaseTamedUnits();   // 00665D20
        unit->SetSide(tamer->side);
        UnitSetPlayer(unit, nullptr, tamerPlayer);
        auto* mode = static_cast<AIMode*>(gamecrt::operator_new(0x24));
        mode->refs = 0;   // written before the test, as the original does (one dword)
        mode->refsHigh = 0;
        if (mode) {
            ModePlayerConstruct(mode);
            ei::SetVtable(mode, std::uintptr_t(0x0073F0C4));   // the player mode's vtable
        }
        static_cast<AIBrain*>(&brain)->AddMode(mode);
        ClearReferenceList<false>(Brain()->seenCorpses);
        ClearReferenceList<true>(Brain()->corpses);
        Stage(this) = 3;
        break;
    }
    default:
        break;
    }
    tamerSide = tamer->side;
    brain.StopAction();
    if (action.kind != 0 && action2.kind != 7 && action2.kind != 8) {
        action2.kind = kOrderNone;
        ReferenceRelease(&action2.target);
        action2.flag18 = 0;
        action2.param = 0;
        action2.modifier = 0;
        action2.kind = 0;
        if (action2.target) {
            ReferenceRelease(&action2.target);
            action2.target = nullptr;
        }
        action2.modifier = 0;
    }
    return 1;
}
EI_REPLACE_METHOD(0x00551AE0, CUnitServer::TryTame);

void CUnitServer::UndoTaming() {
    if (Stage(this) == 0) return;
    BrainRemoveMode(&brain, nullptr, kModeTamed);   // unit_ai.h: the tamed mode (8)
    UnitSetPlayer(unit, nullptr, nullptr);
    unit->SetRelationsB(0xFFFFFFFFu);
    if (Stage(this) > 1) unit->SetSide(ownSide);
    Stage(this) = 0;
}
EI_REPLACE_METHOD(0x00552280, CUnitServer::UndoTaming);
