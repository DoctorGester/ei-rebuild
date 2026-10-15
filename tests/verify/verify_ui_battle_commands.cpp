// Checks for the battle screen's party commands (ui_battle_commands.cpp).
//
// All four give orders through the network view, control units, play phrases or draw random
// numbers, so they cannot run twice: each runs ours only, and the wrapper works out from the state
// before the call what the original's code leaves behind and checks it afterwards.
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/unit.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T Get(const void* p, int offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, sizeof(T));
    return v;
}

constexpr int kMaxParty = 64;

const UiDwordArray* Party(CBattleScreen* self) { return reinterpret_cast<const UiDwordArray*>(&self->party_); }
CUnit* Member(CBattleScreen* self, int i) {
    return reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(Party(self)->data[i]));
}
int PartySize(CBattleScreen* self) {
    int n = Party(self)->size;
    return n < kMaxParty ? n : kMaxParty;
}
HudPortraitSlot& Slot(CBattleScreen* self, int i) { return static_cast<CHudPortraits*>(self->portraits_)->slots_[i]; }
int Marker(CBattleScreen* self, int i) {
    return reinterpret_cast<CUiMesh*>(self->portraits_->sceneObjects_.data[i * 5 + 4])->visible_;
}

// Every member selected before an order: its portrait nods (kind 1; 2 when the order made the
// unit answer at once through slot 32) after a delay that is not positive.
int FirstNotNodding(CBattleScreen* self, const bool* selected, int count) {
    for (int i = 0; i < count; ++i) {
        if (!selected[i]) continue;
        const HudPortraitSlot& slot = Slot(self, i);
        if ((slot.wobbleKind_ != 1 && slot.wobbleKind_ != 2) || slot.wobbleTime_ > 0.0f) return i;
    }
    return -1;
}

int SelectedMembers(CBattleScreen* self, bool* selected) {
    int count = PartySize(self);
    for (int i = 0; i < count; ++i) {
        CUnit* member = Member(self, i);
        selected[i] = member && (member->flags_ & 0x10);
    }
    return count;
}

} // namespace

// CBattleScreen::ToggleMember (006379E0): ours only. Reached by a click on a party member's
// portrait (CHudPortraits::OnMouseUp -> SelectUnit 00638EF0 -> the plain action on a member;
// ui_hud and ui_battle_commands click the hero's portrait) or body, and by the member keys of
// 0063A560. Checked, from the members' flags and the interface's +0x7D before the call: on a
// network client or for an index past the party nothing changes (flags, markers); otherwise each
// portrait's marker (+0x220) holds the chosen value (the flag 0x10 bits, the clicked one flipped
// to 1 / 0 with +0x7D, or only the clicked one 1), its face's rest orientation is the identity
// exactly when not chosen, and every chosen member has flag 0x10 (00566030).
static void* s_toggleMember;
static void __fastcall ToggleMemberVerify(CBattleScreen* self, void*, int index) {
    using ClientFn = bool(__cdecl*)();
    bool client = reinterpret_cast<ClientFn>(0x0057F440)();
    int count = PartySize(self);
    std::uint32_t chosen[kMaxParty] = {};
    std::uint32_t flagsBefore[kMaxParty] = {};
    int markersBefore[kMaxParty] = {};
    for (int i = 0; i < count; ++i) {
        CUnit* member = Member(self, i);
        flagsBefore[i] = member ? member->flags_ : 0;
        chosen[i] = member ? (member->flags_ & 0x10) : 0;
        markersBefore[i] = Marker(self, i);
    }
    bool skipped = client || index >= Party(self)->size;
    if (!skipped && index >= 0 && index < count) {
        if (self->interface_->shiftDown_) {
            chosen[index] = chosen[index] == 0 ? 1u : 0u;
        } else {
            for (int i = 0; i < count; ++i) chosen[i] = 0;
            chosen[index] = 1;
        }
    }

    self->ToggleMember(index);

    if (index < 0 || index >= count) {
        if (!skipped) return;   // outside what the wrapper can follow (the original writes out of bounds)
    }
    const auto* identity = reinterpret_cast<const std::uint8_t*>(0x007C8150);
    int wrong = -1;
    for (int i = 0; i < count && wrong < 0; ++i) {
        CUnit* member = Member(self, i);
        if (skipped) {
            if ((member ? member->flags_ : 0) != flagsBefore[i] || Marker(self, i) != markersBefore[i]) wrong = i;
            continue;
        }
        bool rest = std::memcmp(&Slot(self, i).restB_, identity, 16) == 0;
        bool ok = Marker(self, i) == static_cast<int>(chosen[i]) && rest == (chosen[i] == 0);
        if (member && chosen[i] && !(member->flags_ & 0x10)) ok = false;
        if (!ok) wrong = i;
    }
    ei::verify::Check("CBattleScreen::ToggleMember", true, wrong < 0,
                      "index %d of %d (client %d, +0x7D %d): member %d wrong (marker %d, want %u)", index, count,
                      client ? 1 : 0, self->interface_->shiftDown_, wrong, wrong >= 0 ? Marker(self, wrong) : 0,
                      wrong >= 0 ? chosen[wrong] : 0u);
}
EI_VERIFY(0x006379E0, ToggleMemberVerify, 0, s_toggleMember);

// CBattleScreen::AttackUnit (00637190): ours only. Reached from SelectUnit (00638EF0) for a
// click on a hostile unit outside the party (the plain action), or on any live unit with Shift or
// a modifier flag (+0x18D..); ui_battle_commands clicks blocked wolves next to the hero
// ("clickunit", plain and with Ctrl). Checked: every member selected before the call is nodding with a delay not positive.
static void* s_attackUnit;
static void __fastcall AttackUnitVerify(CBattleScreen* self, void*, CUnit* target, int ctrl) {
    bool selected[kMaxParty] = {};
    int count = SelectedMembers(self, selected);
    self->AttackUnit(target, ctrl);
    int wrong = FirstNotNodding(self, selected, count);
    ei::verify::Check("CBattleScreen::AttackUnit", true, wrong < 0, "target %p: member %d not nodding (kind %d)",
                      target, wrong, wrong >= 0 ? Slot(self, wrong).wobbleKind_ : 0);
}
EI_VERIFY(0x00637190, AttackUnitVerify, 0, s_attackUnit);

// CBattleScreen::OrderDeadUnit (006373A0): ours only. Reached from SelectUnit for a click on
// a dead unit (the plain action; ui_battle_commands kills the wolves and clicks their bodies).
// Checked as AttackUnit.
static void* s_orderDeadUnit;
static void __fastcall OrderDeadUnitVerify(CBattleScreen* self, void*, CUnit* target, int ctrl) {
    bool selected[kMaxParty] = {};
    int count = SelectedMembers(self, selected);
    self->OrderDeadUnit(target, ctrl);
    int wrong = FirstNotNodding(self, selected, count);
    ei::verify::Check("CBattleScreen::OrderDeadUnit", true, wrong < 0,
                      "target %p: member %d not nodding (kind %d)", target, wrong,
                      wrong >= 0 ? Slot(self, wrong).wobbleKind_ : 0);
}
EI_VERIFY(0x006373A0, OrderDeadUnitVerify, 0, s_orderDeadUnit);

// CBattleScreen::OnUnitSpeech (006366F0, slot 32): ours only (it plays the phrase). Reached
// through the vtable: the member toggle (event 0), the attack on a much stronger unit (10), a
// box drag, the orders' answers and the units' own speech events. Checked, from the state before:
// for an id the object manager does not know nothing changes; otherwise the top panel's text
// only grows, and when it grew it ends with a line end; event 0x30 gives the members with the id
// the timed face (+0x48 1, +0x4C 0), events 0xB..0x15 their portrait shake (kind 2, delay not
// positive); every other member's (or, for other events, every member's) wobble kind and face
// mode stay as they were.
static void* s_unitSpeech;
static void __fastcall UnitSpeechVerify(CBattleScreen* self, void*, int id, unsigned event, char* extra) {
    using FindFn = void*(__thiscall*)(void*, int);
    void* unit = reinterpret_cast<FindFn>(0x00403F40)(*reinterpret_cast<void**>(0x007AF5F8), id);
    auto topText = [self]() { return *reinterpret_cast<const char* const*>(reinterpret_cast<std::uint8_t*>(self->topPanel_) + 0x164); };
    auto topLength = [&]() { return static_cast<int>(std::strlen(topText())); };
    int lengthBefore = topLength();
    int count = PartySize(self);
    int kindBefore[kMaxParty] = {};
    int faceBefore[kMaxParty] = {};
    for (int i = 0; i < count; ++i) {
        kindBefore[i] = Slot(self, i).wobbleKind_;
        faceBefore[i] = Slot(self, i).faceMode_;
    }

    self->OnUnitSpeech(id, event, extra);

    int length = topLength();
    bool textOk = unit ? (length >= lengthBefore && (length == lengthBefore || topText()[length - 1] == '\n'))
                       : length == lengthBefore;
    int wrong = -1;
    for (int i = 0; i < count && wrong < 0; ++i) {
        CUnit* member = Member(self, i);
        const HudPortraitSlot& slot = Slot(self, i);
        bool target = unit && member && member->id_ == id;
        bool ok;
        if (target && event == 0x30)
            ok = slot.faceMode_ == 1 && slot.faceTimer_ == 0.0f && slot.wobbleKind_ == kindBefore[i];
        else if (target && event - 0xB <= 0xA)
            ok = slot.wobbleKind_ == 2 && !(slot.wobbleTime_ > 0.0f) && slot.faceMode_ == faceBefore[i];
        else
            ok = slot.wobbleKind_ == kindBefore[i] && slot.faceMode_ == faceBefore[i];
        if (!ok) wrong = i;
    }
    ei::verify::Check("CBattleScreen::OnUnitSpeech", true, textOk && wrong < 0,
                      "id %d (found %d) event 0x%X: top text %d -> %d chars, member %d wrong", id, unit ? 1 : 0, event,
                      lengthBefore, length, wrong);
}
EI_VERIFY(0x006366F0, UnitSpeechVerify, 0, s_unitSpeech);

// The pending spell or quick item used (00637500 on a unit, 00637750 at a point): ours only (they
// give orders through the network view and draw random numbers). Checked, from the state before
// the call: each selected member that the unit's own check (CUnit::CanCastSpell 0052F820 for a
// spell, CanUseQuickItem 0052F8B0 for a quick item, with the pending slot +0x180) allows is
// nodding (kind 1 or 2) with a delay not positive; afterwards nothing is pending (+0x17C 0,
// +0x180 / +0x184 -1) and the quick item, spell and action panels highlight nothing (+0x150 -1).
namespace {
using UnitCheckFn = int(__thiscall*)(CUnit*, int, const float*);
void AllowedMembers(CBattleScreen* self, bool quick, bool* allowed, int count) {
    auto check = reinterpret_cast<UnitCheckFn>(quick ? 0x0052F8B0 : 0x0052F820);
    const float none[3] = {};
    for (int i = 0; i < count; ++i) {
        CUnit* member = Member(self, i);
        allowed[i] = member && (member->flags_ & 0x10) && check(member, self->pendingSlot_, none) == 0;
    }
}
bool NothingPending(CBattleScreen* self) {
    return self->pendingAction_ == 0 && self->pendingSlot_ == -1 && self->pendingItem_ == -1 &&
           Get<int>(self->quickItems_, 0x150) == -1 && Get<int>(self->spellColumn_, 0x150) == -1 &&
           Get<int>(self->actionColumn_, 0x150) == -1;
}
} // namespace

// CBattleScreen::UseOnUnit (00637500). Reached from SelectUnit (00638EF0) with a spell on a
// unit pending (+0x17C 1; the "quick" argument is the Ctrl byte there) or a quick item (3);
// scenario ui_battle_spells (a spell picked in the spell column, then "clickunit" on a wolf).
static void* s_useOnUnit;
static void __fastcall UseOnUnitVerify(CBattleScreen* self, void*, CUnit* target, char quick, int ctrl) {
    bool allowed[kMaxParty] = {};
    int count = PartySize(self);
    int slot = self->pendingSlot_;
    AllowedMembers(self, quick != 0, allowed, count);
    self->UseOnUnit(target, quick, ctrl);
    int wrong = FirstNotNodding(self, allowed, count);
    ei::verify::Check("CBattleScreen::UseOnUnit", true, wrong < 0 && NothingPending(self),
                      "target %p quick %d slot %d: member %d not nodding, pending %d/%d", target, quick, slot, wrong,
                      self->pendingAction_, self->pendingSlot_);
}
EI_VERIFY(0x00637500, UseOnUnitVerify, 0, s_useOnUnit);

// CBattleScreen::UseAtPoint (00637750). Reached from SelectUnit with a spell on a point
// pending (+0x17C 2 or 4); scenario ui_battle_spells (a point spell picked, then a ground click).
// Checked as UseOnUnit.
static void* s_useAtPoint;
static void __fastcall UseAtPointVerify(CBattleScreen* self, void*, float x, float y, float z, CUnit* unit, char quick) {
    bool allowed[kMaxParty] = {};
    int count = PartySize(self);
    int slot = self->pendingSlot_;
    AllowedMembers(self, quick != 0, allowed, count);
    self->UseAtPoint(x, y, z, unit, quick);
    int wrong = FirstNotNodding(self, allowed, count);
    ei::verify::Check("CBattleScreen::UseAtPoint", true, wrong < 0 && NothingPending(self),
                      "unit %p quick %d slot %d: member %d not nodding, pending %d/%d", unit, quick, slot, wrong,
                      self->pendingAction_, self->pendingSlot_);
}
EI_VERIFY(0x00637750, UseAtPointVerify, 0, s_useAtPoint);
