// The battle screen's party commands: the member toggle (006379E0), the attack on a unit
// (00637190), the order on a dead unit (006373A0), a unit's speech event (006366F0, slot 32) and
// the pending spell or quick item used on a unit or a point (00637500, 00637750).
// See docs/notes/ui.md ("Sixteenth pass", "Seventeenth pass").
#include "maths.h"
#include "originals_core.h"
#include "originals_world.h"
#include "random.h"
#include "text_reader.h"
#include "ui_camp.h"
#include "unit.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_ui.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& Field(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
}

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
struct OrderUnits {
    void* vtable;
    CUnit** data;
    int size;
    int capacity;
    int growBy;
};
__declspec(naked) void __fastcall OrderUnitsSetSize(OrderUnits*, void*, int, int) EI_ORIGINAL(0x005EBDB0)
// The toggle's CArray<DWORD> (vtable 0073C3D0; 00460500 SetAtGrow, RET 8).
__declspec(naked) void __fastcall ChosenSetAtGrow(UiDwordArray*, void*, int, std::uint32_t) EI_ORIGINAL(0x00460500)

// The client world (007AF5B8, thiscall): 005699F0 the local view, 00566100 (RET 4) with 0 clears
// the selection, 00566030 (RET 4) controls a unit.
__declspec(naked) std::uint8_t* __fastcall LocalView(void*) EI_ORIGINAL(0x005699F0)
__declspec(naked) void __fastcall WorldClearSelection(void*, void*, CUnit*) EI_ORIGINAL(0x00566100)
__declspec(naked) void __fastcall WorldControlUnit(void*, void*, CUnit*) EI_ORIGINAL(0x00566030)
// The local view's orders (thiscall): 0066A880 attack (units, target, kind, ctrl; RET 0x10),
// 0066AB80 a dead unit (units, target, ctrl; RET 0xC).
__declspec(naked) void __fastcall ViewOrderAttack(void*, void*, OrderUnits*, CUnit*, int, int) EI_ORIGINAL(0x0066A880)
__declspec(naked) void __fastcall ViewOrderDead(void*, void*, OrderUnits*, CUnit*, int) EI_ORIGINAL(0x0066AB80)
// 00532D40 (thiscall; rewritten as CUnit::Level): the unit's level.
__declspec(naked) int __fastcall UnitLevelAt(CUnit*) EI_ORIGINAL(0x00532D40)
// 00637DC0 (thiscall; rewritten): the selection's refresh, through its address.
__declspec(naked) void __fastcall RefreshSelectionAt(CBattleScreenSetup*) EI_ORIGINAL(0x00637DC0)

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
__declspec(naked) void __fastcall ViewSpellAtPoint(void*, void*, int, CUnit*, float*, int) EI_ORIGINAL(0x0066ACD0)
__declspec(naked) void __fastcall ViewQuickAtPoint(void*, void*, int, CUnit*, float*, int) EI_ORIGINAL(0x0066ADD0)
// CUnit::CanCastSpell (0052F820) / CanUseQuickItem (0052F8B0) (thiscall, RET 8; rewritten),
// CHudQuickItems::SetUnit (0063D2B0, RET 4; rewritten), through their addresses.
__declspec(naked) int __fastcall UnitCanCastSpellAt(CUnit*, void*, int, float*) EI_ORIGINAL(0x0052F820)
__declspec(naked) int __fastcall UnitCanUseQuickItemAt(CUnit*, void*, int, float*) EI_ORIGINAL(0x0052F8B0)
__declspec(naked) void __fastcall QuickItemsSetUnitAt(CUiWindow*, void*, CUnit*) EI_ORIGINAL(0x0063D2B0)
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
void DropPendingAction(CBattleScreenSetup* self, bool inlinePanels) {
    CUiWindow* quick = self->quickItems_;
    self->unknown_180 = -1;
    self->unknown_17C = 0;
    Field<int>(self, 0x184) = -1;
    for (int i = 0; i < kQuickSlots; ++i) QuickPanelSet(quick, nullptr, i, 0);
    Field<int>(quick, 0x150) = -1;
    CUiWindow* const panels[2] = {self->spellColumn_, self->actionColumn_};
    for (CUiWindow* panel : panels) {
        for (int i = 0; i < Field<int>(panel, 0x14C); ++i) {
            if (inlinePanels) {
                void* sprite = Field<void**>(panel, 0x108)[i];
                SpriteSetScale(sprite, nullptr, kScaleOne);
                Field<std::uint32_t>(sprite, 0xD8) = kSpriteOpaque;
                Field<int>(panel, 0x150) = i;
            } else if (panel == self->spellColumn_) {
                SpellPanelSet(panel, nullptr, i, 0);
            } else {
                ActionPanelSet(panel, nullptr, i, 0);
            }
        }
        Field<int>(panel, 0x150) = -1;
    }
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
    auto unitAt = [party](int i) { return reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data_[i])); };
    if (IsNetworkClientOriginal()) return;
    if (index >= party->size_) return;

    UiDwordArray chosen{reinterpret_cast<void*>(0x0073C3D0), nullptr, 0, 0, 0};
    for (int i = 0; i < party->size_; ++i) {
        CUnit* member = unitAt(i);
        ChosenSetAtGrow(&chosen, nullptr, chosen.size_, member ? (member->flags_ & kFlagSelected) : 0);
    }
    if (interface_->shiftDown_) {
        chosen.data_[index] = chosen.data_[index] == 0 ? 1u : 0u;
    } else {
        for (int i = 0; i < chosen.size_; ++i)
            if (chosen.data_[i] != 0) chosen.data_[i] = 0;
        chosen.data_[index] = 1;
    }
    WorldClearSelection(kWorld, nullptr, nullptr);

    for (int i = 0; i < party->size_; ++i) {
        auto* portraits = static_cast<CHudPortraits*>(portraits_);
        std::uint32_t value = chosen.data_[i];
        reinterpret_cast<CUiMesh*>(portraits->sceneObjects_.data_[i * 5 + 4])->visible_ = static_cast<int>(value);
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
        if (member && chosen.data_[i] != 0) {
            WorldControlUnit(kWorld, nullptr, member);
            if (index == i) reinterpret_cast<SlotEvent>(vtable_[0x80 / 4])(this, unitAt(i)->id_, 0, 0);
        }
    }

    // Single player: the local player's unit records (0x784 bytes, +0x1C the unit id) keep the
    // members' selection (+0x778), record i against member i.
    if (*reinterpret_cast<const int*>(0x007B0098) == 1) {
        if (auto* player = static_cast<std::uint8_t*>(WorldGetPlayer(kServerWorld, nullptr, 0))) {
            for (int i = 0; i < Field<int>(player, 0xA4); ++i) {
                if (i >= party->size_) continue;
                std::uint8_t* record = Field<std::uint8_t*>(player, 0xA0) + i * 0x784;
                CUnit* member = unitAt(i);
                if (member->id_ == Field<int>(record, 0x1C))
                    Field<std::uint8_t>(record, 0x778) = static_cast<std::uint8_t>((member->flags_ >> 4) & 1);
            }
        }
    }
    RefreshSelectionAt(this);
    if (chosen.data_) gamecrt::operator_delete(chosen.data_);
}
EI_REPLACE_METHOD(0x006379E0, CBattleScreenSetup::ToggleMember);

// --- 00637190 / 006373A0: the selected members ordered at a unit -------------------------------

void CBattleScreenSetup::AttackUnit(CUnit* target, int ctrl) {
    auto* party = reinterpret_cast<UiDwordArray*>(partyArray_);
    auto unitAt = [party](int i) { return reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data_[i])); };
    int kind = 0;
    while (kind < 6 && unknown_18D[kind] == 0) ++kind;

    OrderUnits units{reinterpret_cast<void*>(0x007402AC), nullptr, 0, 0, 0};
    for (int i = 0; i < party->size_; ++i) {
        CUnit* member = unitAt(i);
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        int n = units.size;
        OrderUnitsSetSize(&units, nullptr, n + 1, -1);
        units.data[n] = member;
        HudPortraitSlot& slot = static_cast<CHudPortraits*>(portraits_)->slots_[i];
        slot.wobbleTime_ = PortraitDelay(UiRandom().Next());
        static_cast<CHudPortraits*>(portraits_)->slots_[i].wobbleKind_ = 1;
        int targetLevel = UnitLevelAt(target);
        int memberLevel = UnitLevelAt(unitAt(i));
        if (targetLevel - memberLevel > 5) reinterpret_cast<SlotEvent>(vtable_[0x80 / 4])(this, unitAt(i)->id_, 10, 0);
    }
    if (units.size != 0) ViewOrderAttack(LocalView(kWorld), nullptr, &units, target, kind, ctrl & 0xFF);
    units.vtable = reinterpret_cast<void*>(0x007402AC);
    if (units.data) gamecrt::operator_delete(units.data);
}
EI_REPLACE_METHOD(0x00637190, CBattleScreenSetup::AttackUnit);

void CBattleScreenSetup::OrderDeadUnit(CUnit* target, int ctrl) {
    auto* party = reinterpret_cast<UiDwordArray*>(partyArray_);
    OrderUnits units{reinterpret_cast<void*>(0x007402AC), nullptr, 0, 0, 0};
    for (int i = 0; i < party->size_; ++i) {
        auto* member = reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data_[i]));
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        int n = units.size;
        OrderUnitsSetSize(&units, nullptr, n + 1, -1);
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
    auto unitAt = [party](int i) { return reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data_[i])); };
    for (int i = 0; i < party->size_; ++i) {
        CUnit* member = unitAt(i);
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        float position[3];   // the target's position, passed to the (unused) argument of the checks
        std::memcpy(position, reinterpret_cast<std::uint8_t*>(target) + 0x1C, sizeof(position));
        int refused;
        if (quick == 0) {
            ViewSpellOnUnit(LocalView(kWorld), nullptr, unknown_180, member, target, ctrl & 0xFF);
            refused = UnitCanCastSpellAt(unitAt(i), nullptr, unknown_180, position);
        } else {
            ViewQuickOnUnit(LocalView(kWorld), nullptr, unknown_180, member, target, ctrl & 0xFF);
            QuickItemsSetUnitAt(quickItems_, nullptr, unitAt(i));
            refused = UnitCanUseQuickItemAt(unitAt(i), nullptr, unknown_180, position);
        }
        if (refused == 0) NodPortrait(this, i);
    }
    DropPendingAction(this, true);
}
EI_REPLACE_METHOD(0x00637500, CBattleScreenSetup::UseOnUnit);

void CBattleScreenSetup::UseAtPoint(float x, float y, float z, CUnit* unit, char quick) {
    auto* party = reinterpret_cast<UiDwordArray*>(partyArray_);
    auto unitAt = [party](int i) { return reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data_[i])); };
    // The original works on its argument slots, so the shift below stays for the later members.
    float point[3] = {x, y, z};
    for (int i = 0; i < party->size_; ++i) {
        CUnit* member = unitAt(i);
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        if (member == unit) {
            Vector3 axis;
            reinterpret_cast<const Quaternion*>(reinterpret_cast<std::uint8_t*>(unit) + 0x34)->AxisY(&axis);
            const float k = *reinterpret_cast<const volatile float*>(0x0073B850);   // 0.1
            point[0] = point[0] + -axis.x * k;
            point[1] = point[1] + -axis.y * k;
            point[2] = point[2] + -axis.z * k;
        }
        int refused;
        if (quick != 0) {
            ViewQuickAtPoint(LocalView(kWorld), nullptr, unknown_180, unitAt(i), point, 0);
            QuickItemsSetUnitAt(quickItems_, nullptr, unitAt(i));
            refused = UnitCanUseQuickItemAt(unitAt(i), nullptr, unknown_180, point);
        } else {
            ViewSpellAtPoint(LocalView(kWorld), nullptr, unknown_180, unitAt(i), point, 0);
            refused = UnitCanCastSpellAt(unitAt(i), nullptr, unknown_180, point);
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
        const char* name = Field<const char*>(phrase, 0x1C);
        BOOL found;
        {
            mfc::CString say(reinterpret_cast<const char*>(0x0079430C));   // "say "
            mfc::CString key = say + name;
            found = TextOpen(texts, nullptr, key);
        }
        if (found) {
            TextReadToHash(texts, nullptr, &text, 1);
            text.TrimRight(reinterpret_cast<const char*>(0x0078DD00));   // "\r\n"
            text += reinterpret_cast<const char*>(0x00784340);           // "\n"
            // Both go to the top panel's text and, unless its +0x160, the message log.
            auto post = [this](const char* part) {
                CUiWindow* top = topPanel_;
                *reinterpret_cast<mfc::CString*>(reinterpret_cast<std::uint8_t*>(top) + 0x164) += part;
                if (Field<int>(top, 0x160) == 0) {
                    CUiWindow* screen = Field<CUiWindow*>(top, 0x148);
                    LogAddLine(Field<CUiWindow**>(screen, 0x148)[9], nullptr, part);
                }
            };
            post(line);
            post(text);
        }
    }

    auto* party = reinterpret_cast<UiDwordArray*>(partyArray_);
    auto unitAt = [party](int i) { return reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data_[i])); };
    if (event == 0x30) {
        // The member's timed face (no null test, as the original).
        for (int i = 0; i < party->size_; ++i) {
            if (unitAt(i)->id_ != id) continue;
            HudPortraitSlot* slots = static_cast<CHudPortraits*>(portraits_)->slots_;
            slots[i].faceTimer_ = 0.0f;
            static_cast<CHudPortraits*>(portraits_)->slots_[i].faceMode_ = 1;
        }
    } else if (event - 0xB <= 0x15 - 0xB) {
        for (int i = 0; i < party->size_; ++i) {
            if (unitAt(i)->id_ != id) continue;
            static_cast<CHudPortraits*>(portraits_)->slots_[i].wobbleTime_ = PortraitDelay(UiRandom().Next());
            static_cast<CHudPortraits*>(portraits_)->slots_[i].wobbleKind_ = 2;
        }
    }
}
namespace {
void __fastcall BattleScreenUnitSpeech(CBattleScreenSetup* self, void*, int id, unsigned event, char* extra) {
    self->OnUnitSpeech(id, event, extra);
}
} // namespace
EI_REPLACE(0x006366F0, BattleScreenUnitSpeech);
