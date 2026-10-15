// The battle screen's party commands: the member toggle (006379E0), the attack on a unit
// (00637190), the order on a dead unit (006373A0), a unit's speech event (006366F0, slot 32) and
// the pending spell or quick item used on a unit or a point (00637500, 00637750).
// See docs/notes/ui.md ("Battle screen").
#include "maths.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_world.h"
#include "random.h"
#include "text_reader.h"
#include "ui_camp.h"
#include "unit.h"
#include "unit_client_models.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_ui.h"
#include "originals_units.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr std::uint32_t kFlagSelected = 0x10;
void* const kWorld = reinterpret_cast<void*>(0x007AF5B8);
void* const kServerWorld = reinterpret_cast<void*>(0x007AFF60);
void* const kTextGlue = reinterpret_cast<void*>(0x007AFEA4);

IsaacRandom& UiRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }
// A random value times ~-2^-34 * 0.3 (00742760): the portrait's wobble delay, stored as a float.
float PortraitDelay(std::uint32_t random) {
    return static_cast<float>(x87::FromUnsigned(random) * *reinterpret_cast<const volatile double*>(0x00742760));
}

// The orders' unit list: an MFC CArray<CUnit*> on the stack (vtable 007402AC, SetSize 005EBDB0,
// RET 8); its data is freed with operator delete.
using OrderUnits = mfc::CArray<CUnit*>;
// The toggle's CArray<DWORD> (vtable 0073C3D0; 00460500 SetAtGrow, RET 8).

// The client world (007AF5B8, thiscall): 005699F0 the local view, 00566100 (RET 4) with 0 clears
// the selection, 00566030 (RET 4) controls a unit.
inline std::uint8_t* LocalView(void* client) { return ClientWorldLocalViewOriginal(client); }
// The local view's orders (thiscall): 0066A880 attack (units, target, kind, ctrl; RET 0x10;
// LocalViewSendAttackOriginal), 0066AB80 a dead unit (units, target, ctrl; RET 0xC).
__declspec(naked) void __fastcall ViewOrderDead(void*, void*, OrderUnits*, CUnit*, int) EI_ORIGINAL(0x0066AB80)
// 00532D40 (thiscall; rewritten as CUnit::Level): the unit's level.

// Speech: 006D8430 (__fastcall: unit, event) and 006D8A40 (__fastcall: unit, text, flag; RET 4)
// pick a phrase record (its +0x1C the phrase's name) or return null.
__declspec(naked) std::uint8_t* __fastcall SpeechPhrase(CUnit*, unsigned) EI_ORIGINAL(0x006D8430)
__declspec(naked) std::uint8_t* __fastcall SpeechPhraseFor(CUnit*, char*, bool) EI_ORIGINAL(0x006D8A40)
// 0063B920 (thiscall on the battle screen, RET 8): the unit's name line into a CString.
void __fastcall UnitNameLine(CBattleScreenSetup* a0, void*, CUnit* a2, mfc::CString* a3) { BattleShowPanelUnitOriginal((void*)a0, nullptr, (void*)a2, (void*)a3); }
// 00578E20 (thiscall on 007AFEA4): the text manager.
CTextManager* __fastcall TextTable(void*) { return (CTextManager*)GameFlowOriginal(); }
// 0065CF30 (thiscall on the message log, RET 4): adds a line.
void __fastcall LogAddLine(CUiWindow* a0, void*, const char* a2) { LogAddTextOriginal((void*)a0, nullptr, a2); }

using SlotEvent = void(__thiscall*)(void*, int, int, int);

// The local view's spell and quick item orders (thiscall, RET 0x10): on a unit 0066AC70 (spell) /
// 0066AD40 (quick item) with (slot, member, target, ctrl), at a point 0066ACD0 / 0066ADD0 with
// (slot, member, &point, 0).
__declspec(naked) void __fastcall ViewSpellOnUnit(void*, void*, int, CUnit*, CUnit*, int) EI_ORIGINAL(0x0066AC70)
__declspec(naked) void __fastcall ViewQuickOnUnit(void*, void*, int, CUnit*, CUnit*, int) EI_ORIGINAL(0x0066AD40)
__declspec(naked) void __fastcall ViewSpellAtPoint(void*, void*, int, CUnit*, Vector3*, int) EI_ORIGINAL(0x0066ACD0)
__declspec(naked) void __fastcall ViewQuickAtPoint(void*, void*, int, CUnit*, Vector3*, int) EI_ORIGINAL(0x0066ADD0)
// CUnit::CanCastSpell (0052F820) / CanUseQuickItem (0052F8B0) (thiscall, RET 8; rewritten),
// CHudQuickItems::SetUnit (0063D2B0, RET 4; rewritten), through their addresses.
// The HUD panels (thiscall, RET 8): quick items 0063CD20 (slot, 0), spells 0063E750 (index, 0),
// actions 0063F730 (index, 0); 005E7060 (RET 4) a sprite's scale.
void __fastcall QuickPanelSet(CUiWindow* a0, void*, int a2, int a3) { QuickPanelSetOriginal((void*)a0, nullptr, a2, a3); }
void __fastcall SpellPanelSet(CUiWindow* a0, void*, int a2, int a3) { SpellPanelSetOriginal((void*)a0, nullptr, a2, a3); }
void __fastcall ActionPanelSet(CUiWindow* a0, void*, int a2, int a3) { ActionPanelSetOriginal((void*)a0, nullptr, a2, a3); }
void __fastcall SpriteSetScale(void* a0, void*, std::uint32_t a2) { SpriteSetScaleOriginal(a0, nullptr, a2); }
constexpr std::uint32_t kScaleOne = 0x3F800000;     // 1.0f
constexpr std::uint32_t kSpriteOpaque = 0xFF000000u;
constexpr int kQuickSlots = 8;

// After a spell or quick item was used: no pending action and no highlighted slot in the quick
// item panel (0063CD20 per slot). The spell and action panels: 00637500 resets each sprite inline
// (scale 1, colour opaque, the highlight index walking along), 00637750 calls their setters.
template <typename Column>
void DropColumn(Column* panel, bool inlinePanels, void(__fastcall* setPanel)(CUiWindow*, void*, int, int)) {
    for (int i = 0; i < panel->count_; ++i) {
        if (inlinePanels) {
            CUiMesh* sprite = reinterpret_cast<CUiMesh*>(panel->sceneObjects_.data[i]);
            SpriteSetScale(sprite, nullptr, kScaleOne);
            sprite->colour_ = kSpriteOpaque;
            panel->selected_ = i;
        } else {
            setPanel(panel, nullptr, i, 0);
        }
    }
    panel->selected_ = -1;
}

void DropPendingAction(CBattleScreenSetup* self, bool inlinePanels) {
    CHudQuickItems* quick = static_cast<CHudQuickItems*>(self->quickItems_);
    self->pendingSlot_ = -1;
    self->pendingAction_ = 0;
    self->pendingItem_ = -1;
    for (int i = 0; i < kQuickSlots; ++i) QuickPanelSet(quick, nullptr, i, 0);
    quick->selected_ = -1;
    DropColumn(static_cast<CHudSpellColumn*>(self->spellColumn_), inlinePanels, SpellPanelSet);
    DropColumn(static_cast<CHudActionColumn*>(self->actionColumn_), inlinePanels, ActionPanelSet);
}

// Member i's portrait nods after a random delay (as the orders' nod).
void NodPortrait(CBattleScreenSetup* self, int i) {
    float delay = PortraitDelay(UiRandom().Next());
    static_cast<CHudPortraits*>(self->portraits_)->slots_[i].wobbleTime_ = delay;
    static_cast<CHudPortraits*>(self->portraits_)->slots_[i].wobbleKind_ = 1;
}

} // namespace

// --- 006379E0: a party member toggled ----------------------------------------------------------

void CBattleScreenSetup::ToggleMember(int index) {
    auto* party = reinterpret_cast<UiDwordArray*>(partyArray_);
    auto unitAt = [party](int i) { return reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data[i])); };
    if (IsNetworkClientOriginal()) return;
    if (index >= party->size) return;

    UiDwordArray chosen{reinterpret_cast<void*>(0x0073C3D0), nullptr, 0, 0, 0};
    for (int i = 0; i < party->size; ++i) {
        CUnit* member = unitAt(i);
        DwordArraySetAtGrowOriginal(&chosen, nullptr, chosen.size, member ? (member->flags_ & kFlagSelected) : 0);
    }
    if (interface_->shiftDown_) {
        chosen.data[index] = chosen.data[index] == 0 ? 1u : 0u;
    } else {
        for (int i = 0; i < chosen.size; ++i)
            if (chosen.data[i] != 0) chosen.data[i] = 0;
        chosen.data[index] = 1;
    }
    ClientWorldSelectOriginal(kWorld, nullptr, nullptr);

    for (int i = 0; i < party->size; ++i) {
        auto* portraits = static_cast<CHudPortraits*>(portraits_);
        std::uint32_t value = chosen.data[i];
        reinterpret_cast<CUiMesh*>(portraits->sceneObjects_.data[i * 5 + 4])->visible_ = static_cast<int>(value);
        Quaternion turn;
        const Quaternion* rest = reinterpret_cast<const Quaternion*>(0x007C8150);   // identity
        if (value != 0) {
            const Vector3 axis = {-1.0f, 0.0f, 0.0f};
            const float angle = *reinterpret_cast<const volatile float*>(0x007C8144) *
                                *reinterpret_cast<const volatile float*>(0x0073CDCC);   // 1/7
            rest = turn.SetAxisAngle(axis, angle);
        }
        portraits->slots_[i].restB_ = *rest;
        CUnit* member = unitAt(i);
        if (member && chosen.data[i] != 0) {
            ClientWorldControlUnitOriginal(kWorld, nullptr, member);
            if (index == i) reinterpret_cast<SlotEvent>(vtable_[0x80 / 4])(this, unitAt(i)->id_, 0, 0);
        }
    }

    // Single player: the local player's unit records (0x784 bytes, +0x1C the unit id) keep the
    // members' selection (+0x778), record i against member i.
    if (*reinterpret_cast<const int*>(0x007B0098) == 1) {
        if (auto* player = static_cast<WorldPlayer*>(WorldGetPlayer(kServerWorld, nullptr, 0))) {
            for (int i = 0; i < player->mercCount; ++i) {
                if (i >= party->size) continue;
                PartyUnitRecord* record = &reinterpret_cast<PartyUnitRecord*>(player->mercs)[i];
                CUnit* member = unitAt(i);
                if (member->id_ == static_cast<int>(record->unitId))   // the record's unit id
                    record->selected = static_cast<std::uint8_t>((member->flags_ >> 4) & 1);
            }
        }
    }
    BattleRefreshSelectionOriginal(this);
    if (chosen.data) gamecrt::operator_delete(chosen.data);
}
EI_REPLACE_METHOD(0x006379E0, CBattleScreenSetup::ToggleMember);

// --- 00637190 / 006373A0: the selected members ordered at a unit -------------------------------

void CBattleScreenSetup::AttackUnit(CUnit* target, int ctrl) {
    auto* party = reinterpret_cast<UiDwordArray*>(partyArray_);
    auto unitAt = [party](int i) { return reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data[i])); };
    int kind = 0;
    while (kind < 6 && orderKeys_[kind] == 0) ++kind;

    OrderUnits units{reinterpret_cast<void*>(0x007402AC), nullptr, 0, 0, 0};
    for (int i = 0; i < party->size; ++i) {
        CUnit* member = unitAt(i);
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        int n = units.size;
        PartyArraySetSizeOriginal(&units, nullptr, n + 1, -1);
        units.data[n] = member;
        HudPortraitSlot& slot = static_cast<CHudPortraits*>(portraits_)->slots_[i];
        slot.wobbleTime_ = PortraitDelay(UiRandom().Next());
        static_cast<CHudPortraits*>(portraits_)->slots_[i].wobbleKind_ = 1;
        int targetLevel = UnitLevel(target);
        int memberLevel = UnitLevel(unitAt(i));
        if (targetLevel - memberLevel > 5) reinterpret_cast<SlotEvent>(vtable_[0x80 / 4])(this, unitAt(i)->id_, 10, 0);
    }
    if (units.size != 0) LocalViewSendAttackOriginal(LocalView(kWorld), nullptr, &units, target, kind, ctrl & 0xFF);
    units.vtable = reinterpret_cast<void*>(0x007402AC);
    if (units.data) gamecrt::operator_delete(units.data);
}
EI_REPLACE_METHOD(0x00637190, CBattleScreenSetup::AttackUnit);

void CBattleScreenSetup::OrderDeadUnit(CUnit* target, int ctrl) {
    auto* party = reinterpret_cast<UiDwordArray*>(partyArray_);
    OrderUnits units{reinterpret_cast<void*>(0x007402AC), nullptr, 0, 0, 0};
    for (int i = 0; i < party->size; ++i) {
        auto* member = reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data[i]));
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        int n = units.size;
        PartyArraySetSizeOriginal(&units, nullptr, n + 1, -1);
        units.data[n] = member;
        HudPortraitSlot& slot = static_cast<CHudPortraits*>(portraits_)->slots_[i];
        slot.wobbleTime_ = PortraitDelay(UiRandom().Next());
        static_cast<CHudPortraits*>(portraits_)->slots_[i].wobbleKind_ = 1;
    }
    if (units.size != 0) ViewOrderDead(LocalView(kWorld), nullptr, &units, target, ctrl & 0xFF);
    units.vtable = reinterpret_cast<void*>(0x007402AC);
    if (units.data) gamecrt::operator_delete(units.data);
}
EI_REPLACE_METHOD(0x006373A0, CBattleScreenSetup::OrderDeadUnit);

// --- 00637500 / 00637750: the pending spell or quick item used ---------------------------------

void CBattleScreenSetup::UseOnUnit(CUnit* target, char quick, int ctrl) {
    auto* party = reinterpret_cast<UiDwordArray*>(partyArray_);
    auto unitAt = [party](int i) { return reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data[i])); };
    for (int i = 0; i < party->size; ++i) {
        CUnit* member = unitAt(i);
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        Vector3 position;   // the target's position, passed to the (unused) argument of the checks
        std::memcpy(&position, &target->position, sizeof(position));
        int refused;
        if (quick == 0) {
            ViewSpellOnUnit(LocalView(kWorld), nullptr, pendingSlot_, member, target, ctrl & 0xFF);
            refused = UnitCanCastSpell(unitAt(i), nullptr, pendingSlot_, &position.x);
        } else {
            ViewQuickOnUnit(LocalView(kWorld), nullptr, pendingSlot_, member, target, ctrl & 0xFF);
            QuickItemsSetUnit(quickItems_, nullptr, unitAt(i));
            refused = UnitCanUseQuickItem(unitAt(i), nullptr, pendingSlot_, &position.x);
        }
        if (refused == 0) NodPortrait(this, i);
    }
    DropPendingAction(this, true);
}
EI_REPLACE_METHOD(0x00637500, CBattleScreenSetup::UseOnUnit);

void CBattleScreenSetup::UseAtPoint(float x, float y, float z, CUnit* unit, char quick) {
    auto* party = reinterpret_cast<UiDwordArray*>(partyArray_);
    auto unitAt = [party](int i) { return reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data[i])); };
    // The original works on its argument slots, so the shift below stays for the later members.
    Vector3 point = {x, y, z};
    for (int i = 0; i < party->size; ++i) {
        CUnit* member = unitAt(i);
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        if (member == unit) {
            Vector3 axis;
            unit->orientation.AxisY(&axis);
            const float k = *reinterpret_cast<const volatile float*>(0x0073B850);   // 0.1
            point.x = point.x + -axis.x * k;
            point.y = point.y + -axis.y * k;
            point.z = point.z + -axis.z * k;
        }
        int refused;
        if (quick != 0) {
            ViewQuickAtPoint(LocalView(kWorld), nullptr, pendingSlot_, unitAt(i), &point, 0);
            QuickItemsSetUnit(quickItems_, nullptr, unitAt(i));
            refused = UnitCanUseQuickItem(unitAt(i), nullptr, pendingSlot_, &point.x);
        } else {
            ViewSpellAtPoint(LocalView(kWorld), nullptr, pendingSlot_, unitAt(i), &point, 0);
            refused = UnitCanCastSpell(unitAt(i), nullptr, pendingSlot_, &point.x);
        }
        if (refused == 0) NodPortrait(this, i);
    }
    DropPendingAction(this, false);
}
EI_REPLACE_METHOD(0x00637750, CBattleScreenSetup::UseAtPoint);

// --- 006366F0: a unit's speech event (slot 32) -------------------------------------------------

void CBattleScreenSetup::OnUnitSpeech(int id, unsigned event, char* extra) {
    auto* unit = reinterpret_cast<CUnit*>(ManagerFindObject(*reinterpret_cast<void**>(0x007AF5F8), nullptr, id));
    if (!unit) return;
    std::uint8_t* phrase = (event == 0x2A || event == 0x2B) ? SpeechPhraseFor(unit, extra, event == 0x2B)
                                                            : SpeechPhrase(unit, event);
    if (phrase) {
        // The original keeps the phrase's text in its `extra` argument slot.
        mfc::CString text;
        mfc::CString line;
        UnitNameLine(this, nullptr, unit, &line);
        line += reinterpret_cast<const char*>(0x00794314);   // ":\n"
        CTextManager* texts = TextTable(kTextGlue);
        const char* name = reinterpret_cast<const SpeechRecord*>(phrase)->name.data();
        BOOL found;
        {
            mfc::CString say(reinterpret_cast<const char*>(0x0079430C));   // "say "
            mfc::CString key = say + name;
            found = texts->Open(key);
        }
        if (found) {
            texts->ReadToHash(&text, 1);
            text.TrimRight(reinterpret_cast<const char*>(0x0078DD00));   // "\r\n"
            text += reinterpret_cast<const char*>(0x00784340);           // "\n"
            // Both go to the top panel's text and, unless its +0x160, the message log.
            auto post = [this](const char* part) {
                CHudTopPanel* top = static_cast<CHudTopPanel*>(topPanel_);
                top->PageText(0) += part;
                if (top->page_ == 0) LogAddLine(top->owner_->children_.data[9], nullptr, part);
            };
            post(line);
            post(text);
        }
    }

    auto* party = reinterpret_cast<UiDwordArray*>(partyArray_);
    auto unitAt = [party](int i) { return reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data[i])); };
    if (event == 0x30) {
        // The member's timed face (no null test, as the original).
        for (int i = 0; i < party->size; ++i) {
            if (unitAt(i)->id_ != id) continue;
            HudPortraitSlot* slots = static_cast<CHudPortraits*>(portraits_)->slots_;
            slots[i].faceTimer_ = 0.0f;
            static_cast<CHudPortraits*>(portraits_)->slots_[i].faceMode_ = 1;
        }
    } else if (event - 0xB <= 0x15 - 0xB) {
        for (int i = 0; i < party->size; ++i) {
            if (unitAt(i)->id_ != id) continue;
            static_cast<CHudPortraits*>(portraits_)->slots_[i].wobbleTime_ = PortraitDelay(UiRandom().Next());
            static_cast<CHudPortraits*>(portraits_)->slots_[i].wobbleKind_ = 2;
        }
    }
}
EI_REPLACE_METHOD(0x006366F0, CBattleScreenSetup::OnUnitSpeech);
