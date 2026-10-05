// The battle screen's party commands: the member toggle (006379E0), the attack on a unit
// (00637190), the order on a dead unit (006373A0) and a unit's speech event (006366F0, slot 32).
// See docs/notes/ui.md ("Sixteenth pass").
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

#include <cstdint>

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
__declspec(naked) void __fastcall UnitNameLine(CBattleScreenSetup*, void*, CUnit*, mfc::CString*) EI_ORIGINAL(0x0063B920)
// 00578E20 (thiscall on 007AFEA4): the text manager.
__declspec(naked) CTextManager* __fastcall TextTable(void*) EI_ORIGINAL(0x00578E20)
// 0065CF30 (thiscall on the message log, RET 4): adds a line.
__declspec(naked) void __fastcall LogAddLine(CUiWindow*, void*, const char*) EI_ORIGINAL(0x0065CF30)

using SlotEvent = void(__thiscall*)(void*, int, int, int);

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
    if (interface_->keyFlag7D_) {
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
