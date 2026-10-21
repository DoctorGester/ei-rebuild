// The battle screen's party commands: the member toggle (006379E0), the attack on a unit
// (00637190), the order on a dead unit (006373A0), a unit's speech event (006366F0, slot 32) and
// the pending spell or quick item used on a unit or a point (00637500, 00637750).
// See docs/notes/ui.md ("Battle screen").
#include "acks_db.h"
#include "base_manager.h"
#include "maths.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_world.h"
#include "player_client.h"
#include "random.h"
#include "text_reader.h"
#include "ui.h"
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
#include "world_glue.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr std::uint32_t kFlagSelected = 0x10;

// A random value times ~-2^-34 * 0.3 (00742760): the portrait's wobble delay, stored as a float.
float PortraitDelay(std::uint32_t random) {
    return static_cast<float>(x87::FromUnsigned(random) * (-6.984919588798148e-11 /*00742760*/));
}

// The orders' unit list: an MFC CArray<CUnit*> on the stack (vtable 007402AC, SetSize 005EBDB0,
// RET 8); its data is freed with operator delete.
using OrderUnits = mfc::CArray<CUnit*>;
// The toggle's CArray<DWORD> (vtable 0073C3D0; 00460500 SetAtGrow, RET 8).

// The client world (007AF5B8, thiscall): 005699F0 the local view, 00566100 (RET 4) with 0 clears
// the selection, 00566030 (RET 4) controls a unit.
inline CPlayerClient* LocalView(void* client) { return reinterpret_cast<CPlayerClient*>(static_cast<CWorldClient*>(client)->LocalView()); }
// The local view's orders (thiscall): 0066A880 attack (units, target, kind, ctrl; RET 0x10;
// CPlayerClient::SendAttack), 0066AB80 a dead unit (units, target, ctrl; RET 0xC).
// 00532D40 (thiscall; rewritten as CUnit::Level): the unit's level.


using SlotEvent = void(__thiscall*)(void*, int, int, int);

// The local view's spell and quick item orders (thiscall, RET 0x10): on a unit 0066AC70 (spell) /
// 0066AD40 (quick item) with (slot, member, target, ctrl), at a point 0066ACD0 / 0066ADD0 with
// (slot, member, &point, 0).
__declspec(naked) void __fastcall ViewSpellAtPoint(void*, void*, int, CUnit*, Vector3*, int) EI_ORIGINAL(0x0066ACD0)
__declspec(naked) void __fastcall ViewQuickAtPoint(void*, void*, int, CUnit*, Vector3*, int) EI_ORIGINAL(0x0066ADD0)
// CUnit::CanCastSpell (0052F820) / CanUseQuickItem (0052F8B0) (thiscall, RET 8; rewritten),
// CHudQuickItems::SetUnit (0063D2B0, RET 4; rewritten), through their addresses.
// The HUD panels (thiscall, RET 8): quick items 0063CD20 (slot, 0), spells 0063E750 (index, 0),
// actions 0063F730 (index, 0); 005E7060 (RET 4) a sprite's scale.
constexpr std::uint32_t kScaleOne = 0x3F800000;     // 1.0f
constexpr std::uint32_t kSpriteOpaque = 0xFF000000u;
constexpr int kQuickSlots = 8;

// After a spell or quick item was used: no pending action and no highlighted slot in the quick
// item panel (0063CD20 per slot). The spell and action panels: 00637500 resets each sprite inline
// (scale 1, colour opaque, the highlight index walking along), 00637750 calls their setters.
template <typename Column>
void DropColumn(Column* panel, bool inlinePanels) {
    for (int i = 0; i < panel->count_; ++i) {
        if (inlinePanels) {
            CUiMesh* sprite = panel->SceneMesh(i);
            sprite->SetScale(std::bit_cast<float>(kScaleOne));
            sprite->flatSpecular_ = kSpriteOpaque;
            panel->selected_ = i;
        } else {
            panel->Highlight(i, 0);
        }
    }
    panel->selected_ = -1;
}

void DropPendingAction(CBattleScreen* self, bool inlinePanels) {
    CHudQuickItems* quick = self->quickItems_;
    self->pendingSlot_ = -1;
    self->pendingAction_ = 0;
    self->pendingItem_ = -1;
    for (int i = 0; i < kQuickSlots; ++i) quick->Highlight(i, 0);
    quick->selected_ = -1;
    DropColumn(self->spellColumn_, inlinePanels);
    DropColumn(self->actionColumn_, inlinePanels);
}

// Member i's portrait nods after a random delay (as the orders' nod).
void NodPortrait(CBattleScreen* self, int i) {
    float delay = PortraitDelay(IsaacRandom::Global().Next());
    static_cast<CHudPortraits*>(self->portraits_)->slots_[i].wobbleTime_ = delay;
    static_cast<CHudPortraits*>(self->portraits_)->slots_[i].wobbleKind_ = 1;
}

} // namespace

// --- 006379E0: a party member toggled ----------------------------------------------------------

void CBattleScreen::ToggleMember(int index) {
    auto* party = &party_;
    auto unitAt = [party](int i) { return party->data[i]; };
    if (IsMultiplayerClient()) return;
    if (index >= party->size) return;

    UiDwordArray chosen{ei::GamePointer<void>(0x0073C3D0), nullptr, 0, 0, 0};
    for (int i = 0; i < party->size; ++i) {
        CUnit* member = unitAt(i);
        chosen.SetAtGrow(chosen.size, member ? (member->flags_ & kFlagSelected) : 0);
    }
    if (interface_->shiftDown_) {
        chosen.data[index] = chosen.data[index] == 0 ? 1u : 0u;
    } else {
        for (int i = 0; i < chosen.size; ++i)
            if (chosen.data[i] != 0) chosen.data[i] = 0;
        chosen.data[index] = 1;
    }
    TheClientWorld()->Deselect(nullptr);

    for (int i = 0; i < party->size; ++i) {
        auto* portraits = static_cast<CHudPortraits*>(portraits_);
        std::uint32_t value = chosen.data[i];
        portraits->SceneMesh(i * 5 + 4)->visible_ = static_cast<int>(value);
        Quaternion turn;
        const Quaternion* rest = ei::GamePointer<const Quaternion>(0x007C8150);   // identity
        if (value != 0) {
            const Vector3 axis = {-1.0f, 0.0f, 0.0f};
            const float angle = ei::GameData<const volatile float>(0x007C8144) *
                                0.14285715f /*0073CDCC*/;   // 1/7
            rest = turn.SetAxisAngle(axis, angle);
        }
        portraits->slots_[i].restB_ = *rest;
        CUnit* member = unitAt(i);
        if (member && chosen.data[i] != 0) {
            TheClientWorld()->Select(member);
            if (index == i) reinterpret_cast<SlotEvent>(vtable_[0x80 / 4])(this, unitAt(i)->id_, 0, 0);
        }
    }

    // Single player: the local player's unit records (0x784 bytes, +0x1C the unit id) keep the
    // members' selection (+0x778), record i against member i.
    if (ei::GameData<const int>(0x007B0098) == 1) {
        if (auto* player = static_cast<WorldPlayer*>(TheServerWorld()->GetPlayer(0))) {
            for (int i = 0; i < player->mercCount; ++i) {
                if (i >= party->size) continue;
                PartyUnitRecord* record = &reinterpret_cast<PartyUnitRecord*>(player->mercs)[i];
                CUnit* member = unitAt(i);
                if (member->id_ == static_cast<int>(record->unitId))   // the record's unit id
                    record->selected = static_cast<std::uint8_t>((member->flags_ >> 4) & 1);
            }
        }
    }
    RefreshSelection();
    if (chosen.data) gamecrt::operator_delete(chosen.data);
}
EI_REWRITTEN(0x006379E0, CBattleScreen::ToggleMember);

// --- 00637190 / 006373A0: the selected members ordered at a unit -------------------------------

void CBattleScreen::AttackUnit(CUnit* target, int ctrl) {
    auto* party = &party_;
    auto unitAt = [party](int i) { return party->data[i]; };
    int kind = 0;
    while (kind < 6 && orderKeys_[kind] == 0) ++kind;

    OrderUnits units{ei::GamePointer<void>(0x007402AC), nullptr, 0, 0, 0};
    for (int i = 0; i < party->size; ++i) {
        CUnit* member = unitAt(i);
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        int n = units.size;
        units.SetSize(n + 1, -1);
        units.data[n] = member;
        HudPortraitSlot& slot = static_cast<CHudPortraits*>(portraits_)->slots_[i];
        slot.wobbleTime_ = PortraitDelay(IsaacRandom::Global().Next());
        static_cast<CHudPortraits*>(portraits_)->slots_[i].wobbleKind_ = 1;
        int targetLevel = target->Level();
        int memberLevel = unitAt(i)->Level();
        if (targetLevel - memberLevel > 5) reinterpret_cast<SlotEvent>(vtable_[0x80 / 4])(this, unitAt(i)->id_, 10, 0);
    }
    if (units.size != 0) LocalView(TheClientWorld())->SendAttack(&units, target, kind, ctrl & 0xFF);
    units.vtable = ei::GamePointer<void>(0x007402AC);
    if (units.data) gamecrt::operator_delete(units.data);
}
EI_REPLACE_METHOD(0x00637190, CBattleScreen::AttackUnit);

void CBattleScreen::OrderDeadUnit(CUnit* target, int ctrl) {
    auto* party = &party_;
    OrderUnits units{ei::GamePointer<void>(0x007402AC), nullptr, 0, 0, 0};
    for (int i = 0; i < party->size; ++i) {
        auto* member = party->data[i];
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        int n = units.size;
        units.SetSize(n + 1, -1);
        units.data[n] = member;
        HudPortraitSlot& slot = static_cast<CHudPortraits*>(portraits_)->slots_[i];
        slot.wobbleTime_ = PortraitDelay(IsaacRandom::Global().Next());
        static_cast<CHudPortraits*>(portraits_)->slots_[i].wobbleKind_ = 1;
    }
    if (units.size != 0) LocalView(TheClientWorld())->SendUseDead(&units, target, ctrl & 0xFF);
    units.vtable = ei::GamePointer<void>(0x007402AC);
    if (units.data) gamecrt::operator_delete(units.data);
}
EI_REWRITTEN(0x006373A0, CBattleScreen::OrderDeadUnit);

// --- 00637500 / 00637750: the pending spell or quick item used ---------------------------------

void CBattleScreen::UseOnUnit(CUnit* target, char quick, int ctrl) {
    auto* party = &party_;
    auto unitAt = [party](int i) { return party->data[i]; };
    for (int i = 0; i < party->size; ++i) {
        CUnit* member = unitAt(i);
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        Vector3 position;   // the target's position, passed to the (unused) argument of the checks
        std::memcpy(&position, &target->position, sizeof(position));
        int refused;
        if (quick == 0) {
            LocalView(TheClientWorld())->SendCastOnUnit(pendingSlot_, member, target, ctrl & 0xFF);
            refused = unitAt(i)->CanCastSpell(pendingSlot_, &position.x);
        } else {
            LocalView(TheClientWorld())->SendItemOnUnit(pendingSlot_, member, target, ctrl & 0xFF);
            quickItems_->SetUnit(unitAt(i));
            refused = unitAt(i)->CanUseQuickItem(pendingSlot_, &position.x);
        }
        if (refused == 0) NodPortrait(this, i);
    }
    DropPendingAction(this, true);
}
EI_REWRITTEN(0x00637500, CBattleScreen::UseOnUnit);

void CBattleScreen::UseAtPoint(float x, float y, float z, CUnit* unit, char quick) {
    auto* party = &party_;
    auto unitAt = [party](int i) { return party->data[i]; };
    // The original works on its argument slots, so the shift below stays for the later members.
    Vector3 point = {x, y, z};
    for (int i = 0; i < party->size; ++i) {
        CUnit* member = unitAt(i);
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        if (member == unit) {
            Vector3 axis;
            unit->orientation.AxisY(&axis);
            const float k = 0.1f /*0073B850*/;   // 0.1
            point.x = point.x + -axis.x * k;
            point.y = point.y + -axis.y * k;
            point.z = point.z + -axis.z * k;
        }
        int refused;
        if (quick != 0) {
            ViewQuickAtPoint(LocalView(TheClientWorld()), nullptr, pendingSlot_, unitAt(i), &point, 0);
            quickItems_->SetUnit(unitAt(i));
            refused = unitAt(i)->CanUseQuickItem(pendingSlot_, &point.x);
        } else {
            ViewSpellAtPoint(LocalView(TheClientWorld()), nullptr, pendingSlot_, unitAt(i), &point, 0);
            refused = unitAt(i)->CanCastSpell(pendingSlot_, &point.x);
        }
        if (refused == 0) NodPortrait(this, i);
    }
    DropPendingAction(this, false);
}
EI_REWRITTEN(0x00637750, CBattleScreen::UseAtPoint);

// --- 006366F0: a unit's speech event (slot 32) -------------------------------------------------

void CBattleScreen::OnUnitSpeech(int id, unsigned event, char* extra) {
    auto* unit = reinterpret_cast<CUnit*>(TheObjectManager()->FindObject(id));
    if (!unit) return;
    const SpeechRecord* phrase = (event == 0x2A || event == 0x2B) ? acks::SayNamedAck(unit, extra, event == 0x2B)
                                                                  : acks::SayAck(unit, static_cast<int>(event));
    if (phrase) {
        // The original keeps the phrase's text in its `extra` argument slot.
        mfc::CString text;
        mfc::CString line;
        UnitName(unit, &line);
        line += ":\n";
        CTextManager* texts = (CTextManager*)GameClientTexts();
        const char* name = phrase->name.data();
        BOOL found;
        {
            mfc::CString say("say ");
            mfc::CString key = say + name;
            found = texts->Open(key);
        }
        if (found) {
            texts->ReadToHash(&text, 1);
            text.TrimRight("\r\n");
            text += "\n";
            // Both go to the top panel's text and, unless its +0x160, the message log.
            auto post = [this](const char* part) {
                CHudTopPanel* top = static_cast<CHudTopPanel*>(topPanel_);
                top->PageText(0) += part;
                if (top->page_ == 0) static_cast<CUiScrollText*>((void*)top->owner_->children_.data[9])->AddText(part);
            };
            post(line);
            post(text);
        }
    }

    auto* party = &party_;
    auto unitAt = [party](int i) { return party->data[i]; };
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
            static_cast<CHudPortraits*>(portraits_)->slots_[i].wobbleTime_ = PortraitDelay(IsaacRandom::Global().Next());
            static_cast<CHudPortraits*>(portraits_)->slots_[i].wobbleKind_ = 2;
        }
    }
}
EI_REPLACE_METHOD(0x006366F0, CBattleScreen::OnUnitSpeech);
