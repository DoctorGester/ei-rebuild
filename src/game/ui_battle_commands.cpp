// The battle screen's party commands: the member toggle, the attack on a unit
// the order on a dead unit, a unit's speech event and
// the pending spell or quick item used on a unit or a point.
// See docs/notes/ui.md ("Battle screen").
#include "acks_db.h"
#include "base_manager.h"
#include "maths.h"
#include "player_client.h"
#include "random.h"
#include "text_reader.h"
#include "ui.h"
#include "ui_camp.h"
#include "unit.h"
#include "unit_client_models.h"
#include "party.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "world_glue.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <vector>

namespace {

constexpr std::uint32_t kFlagSelected = 0x10;

// A random value times ~-2^-34 * 0.3: the portrait's wobble delay, stored as a float.
float PortraitDelay(std::uint32_t random) {
    return static_cast<float>(x87::FromUnsigned(random) * (-6.984919588798148e-11 /*00742760*/));
}

// The orders' unit list: an MFC CArray<CUnit*> on the stack (SetSize);
// its data is freed with operator delete.
using OrderUnits = std::vector<CUnit*>;

inline CPlayerClient* LocalView(CWorldClient* client) { return client->LocalView(); }

// A sprite's scale and opacity; the HUD's quick item slots.
constexpr std::uint32_t kScaleOne = 0x3F800000;     // 1.0f
constexpr std::uint32_t kSpriteOpaque = 0xFF000000u;
constexpr int kQuickSlots = 8;

// After a spell or quick item was used: no pending action and no highlighted slot in the quick
// item panel (per slot). The spell and action panels: resets each sprite inline
// (scale 1, colour opaque, the highlight index walking along), calls their setters.
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
    self->portraits_->slots_[i].wobbleTime_ = delay;
    self->portraits_->slots_[i].wobbleKind_ = 1;
}

} // namespace

// --- a party member toggled ----------------------------------------------------------------------

void CBattleScreen::ToggleMember(int index) {
    auto* party = &party_;
    auto unitAt = [party](int i) { return (*party)[i]; };
    if (IsMultiplayerClient()) return;
    if (index >= std::ssize(*party)) return;

    UiDwordArray chosen{};
    for (int i = 0; i < std::ssize(*party); ++i) {
        CUnit* member = unitAt(i);
        lib::SetAtGrow(chosen, std::ssize(chosen), member ? (member->flags_ & kFlagSelected) : 0);
    }
    if (interface_->shiftDown_) {
        chosen[index] = chosen[index] == 0 ? 1u : 0u;
    } else {
        for (int i = 0; i < std::ssize(chosen); ++i)
            if (chosen[i] != 0) chosen[i] = 0;
        chosen[index] = 1;
    }
    TheClientWorld()->Deselect(nullptr);

    for (int i = 0; i < std::ssize(*party); ++i) {
        auto* portraits = portraits_;
        std::uint32_t value = chosen[i];
        portraits->SceneMesh(i * 5 + 4)->visible_ = static_cast<int>(value);
        Quaternion turn;
        const Quaternion* rest = &kHudRestRotation;
        if (value != 0) {
            const Vector3 axis = {-1.0f, 0.0f, 0.0f};
            const float angle = kHudQuarterTurn *
                                0.14285715f /*0073CDCC*/;   // 1/7
            rest = turn.SetAxisAngle(axis, angle);
        }
        portraits->slots_[i].restB_ = *rest;
        CUnit* member = unitAt(i);
        if (member && chosen[i] != 0) {
            TheClientWorld()->Select(member);
            if (index == i) Speech(unitAt(i)->id_, 0, nullptr);
        }
    }

    // Single player: the local player's unit records (0x784 bytes, +0x1C the unit id) keep the
    // members' selection, record i against member i.
    if (TheServerWorld()->gameResult_ == 1) {
        if (auto* player = TheServerWorld()->GetPlayer(0)) {
            for (int i = 0; i < std::ssize(player->Party()->members_); ++i) {
                if (i >= std::ssize(*party)) continue;
                PartyUnitRecord* record = &player->Party()->members_[i];
                CUnit* member = unitAt(i);
                if (member->id_ == static_cast<int>(record->unitId))   // the record's unit id
                    record->selected = static_cast<std::uint8_t>((member->flags_ >> 4) & 1);
            }
        }
    }
    RefreshSelection();
}

// --- the selected members ordered at a unit ------------------------------------------------------

void CBattleScreen::AttackUnit(CUnit* target, int ctrl) {
    auto* party = &party_;
    auto unitAt = [party](int i) { return (*party)[i]; };
    int kind = 0;
    while (kind < 6 && orderKeys_[kind] == 0) ++kind;

    OrderUnits units{};
    for (int i = 0; i < std::ssize(*party); ++i) {
        CUnit* member = unitAt(i);
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        int n = std::ssize(units);
        units.resize(n + 1);
        units[n] = member;
        HudPortraitSlot& slot = portraits_->slots_[i];
        slot.wobbleTime_ = PortraitDelay(IsaacRandom::Global().Next());
        portraits_->slots_[i].wobbleKind_ = 1;
        int targetLevel = target->Level();
        int memberLevel = unitAt(i)->Level();
        if (targetLevel - memberLevel > 5) Speech(unitAt(i)->id_, 10, nullptr);
    }
    if (!units.empty()) LocalView(TheClientWorld())->SendAttack(&units, target, kind, ctrl & 0xFF);
}

void CBattleScreen::OrderDeadUnit(CUnit* target, int ctrl) {
    auto* party = &party_;
    OrderUnits units{};
    for (int i = 0; i < std::ssize(*party); ++i) {
        auto* member = (*party)[i];
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        int n = std::ssize(units);
        units.resize(n + 1);
        units[n] = member;
        HudPortraitSlot& slot = portraits_->slots_[i];
        slot.wobbleTime_ = PortraitDelay(IsaacRandom::Global().Next());
        portraits_->slots_[i].wobbleKind_ = 1;
    }
    if (!units.empty()) LocalView(TheClientWorld())->SendUseDead(&units, target, ctrl & 0xFF);
}

// --- the pending spell or quick item used --------------------------------------------------------

void CBattleScreen::UseOnUnit(CUnit* target, char quick, int ctrl) {
    auto* party = &party_;
    auto unitAt = [party](int i) { return (*party)[i]; };
    for (int i = 0; i < std::ssize(*party); ++i) {
        CUnit* member = unitAt(i);
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        Vector3 position;   // the target's position, passed to the (unused) argument of the checks
        std::memcpy(&position, &target->localPosition_, sizeof(position));
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

void CBattleScreen::UseAtPoint(float x, float y, float z, CUnit* unit, char quick) {
    auto* party = &party_;
    auto unitAt = [party](int i) { return (*party)[i]; };
    // The shift below stays for the later members.
    Vector3 point = {x, y, z};
    for (int i = 0; i < std::ssize(*party); ++i) {
        CUnit* member = unitAt(i);
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        if (member == unit) {
            Vector3 axis;
            unit->localRotation_.AxisY(&axis);
            const float k = 0.1f /*0073B850*/;
            point.x = point.x + -axis.x * k;
            point.y = point.y + -axis.y * k;
            point.z = point.z + -axis.z * k;
        }
        int refused;
        if (quick != 0) {
            LocalView(TheClientWorld())->SendItemAtPoint(pendingSlot_, unitAt(i), &point, 0);
            quickItems_->SetUnit(unitAt(i));
            refused = unitAt(i)->CanUseQuickItem(pendingSlot_, &point.x);
        } else {
            LocalView(TheClientWorld())->SendCastAtPoint(pendingSlot_, unitAt(i), &point, 0);
            refused = unitAt(i)->CanCastSpell(pendingSlot_, &point.x);
        }
        if (refused == 0) NodPortrait(this, i);
    }
    DropPendingAction(this, false);
}

// --- a unit's speech event -----------------------------------------------------------------------

void CBattleScreen::OnUnitSpeech(int id, unsigned event, char* extra) {
    CUnit* unit = UnitWithId(TheObjectManager(), id);
    if (!unit) return;
    const SpeechRecord* phrase = (event == 0x2A || event == 0x2B) ? acks::SayNamedAck(unit, extra, event == 0x2B)
                                                                  : acks::SayAck(unit, static_cast<int>(event));
    if (phrase) {
        // The phrase's text.
        std::string text;
        std::string line;
        UnitName(unit, &line);
        line += ":\n";
        CTextManager* texts = GameClientTexts();
        const char* name = phrase->name.data();
        BOOL found;
        {
            std::string say("say ");
            std::string key = say + name;
            found = texts->Open(key.c_str());
        }
        if (found) {
            texts->ReadToHash(&text, 1);
            str::TrimRight(text, "\r\n");
            text += "\n";
            // Both go to the top panel's text and, unless its +0x160, the message log.
            auto post = [this](const char* part) {
                CHudTopPanel* top = topPanel_;
                top->pageTexts_[0] += part;
                if (top->page_ == 0) top->owner_->MessageLog()->AddText(part);
            };
            post(line.c_str());
            post(text.c_str());
        }
    }

    auto* party = &party_;
    auto unitAt = [party](int i) { return (*party)[i]; };
    if (event == 0x30) {
        // The member's timed face (no null test).
        for (int i = 0; i < std::ssize(*party); ++i) {
            if (unitAt(i)->id_ != id) continue;
            HudPortraitSlot* slots = portraits_->slots_.data();
            slots[i].faceTimer_ = 0.0f;
            portraits_->slots_[i].faceMode_ = 1;
        }
    } else if (event - 0xB <= 0x15 - 0xB) {
        for (int i = 0; i < std::ssize(*party); ++i) {
            if (unitAt(i)->id_ != id) continue;
            portraits_->slots_[i].wobbleTime_ = PortraitDelay(IsaacRandom::Global().Next());
            portraits_->slots_[i].wobbleKind_ = 2;
        }
    }
}
