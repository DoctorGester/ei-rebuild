// The battle screen's party commands: the member toggle, the attack on a unit
// the order on a dead unit, a unit's speech event and
// the pending spell or quick item used on a unit or a point.
// See docs/notes/ui.md ("Battle screen").
#include "acks_db.h"
#include "base_manager.h"
#include "text_manager.h"
#include "maths.h"
#include "player_client.h"
#include "ui.h"
#include "ui_camp.h"
#include "unit.h"
#include "unit_client_models.h"
#include "party.h"
#include "world.h"

#include <cstdint>
#include <cstring>
#include <string>
#include "world_glue.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <vector>

namespace {

// HudPortraitSlot::wobbleKind_.
constexpr int kWobbleNod = 1;
constexpr int kWobbleShake = 2;

} // namespace

// --- a party member toggled ----------------------------------------------------------------------

void CBattleScreen::ToggleMember(int index) {
    auto* party = &party_;
    auto unitAt = [party](int i) { return (*party)[i]; };
    if (IsMultiplayerClient()) return;
    if (index >= std::ssize(*party)) return;

    std::vector<std::uint32_t> chosen{};
    for (int i = 0; i < std::ssize(*party); ++i) {
        CUnit* member = unitAt(i);
        lib::SetAtGrow(chosen, std::ssize(chosen), member ? (member->flags_ & MapObjectFlag::Selected) : 0);
    }
    if (interface_->shiftDown_) {
        chosen[index] = chosen[index] == 0 ? 1u : 0u;
    } else {
        for (int i = 0; i < std::ssize(chosen); ++i)
            if (chosen[i] != 0) chosen[i] = 0;
        chosen[index] = 1;
    }
    g_clientWorld.Deselect(nullptr);

    for (int i = 0; i < std::ssize(*party); ++i) {
        auto* portraits = portraits_;
        std::uint32_t value = chosen[i];
        portraits->SceneMesh(i * 5 + 4)->visible_ = value != 0;
        Quaternion turn;
        const Quaternion* rest = &kHudRestRotation;
        if (value != 0) {
            const Vector3 axis = {-1.0f, 0.0f, 0.0f};
            const float angle = kHudQuarterTurn *
                                0.14285715f;   // 1/7
            rest = turn.SetAxisAngle(axis, angle);
        }
        portraits->slots_[i].restB_ = *rest;
        CUnit* member = unitAt(i);
        if (member && chosen[i] != 0) {
            g_clientWorld.Select(member);
            if (index == i) Speech(unitAt(i)->id_, AckKind::Selected, nullptr);
        }
    }

    // Single player: the local player's unit records (0x784 bytes, +0x1C the unit id) keep the
    // members' selection, record i against member i.
    if (g_serverWorld.gameResult_ == 1) {
        if (auto* player = g_serverWorld.GetPlayer(0)) {
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

std::vector<int> CBattleScreen::SelectedIndexes() const {
    std::vector<int> indexes;
    for (int i = 0; i < std::ssize(party_); ++i) {
        const CUnit* member = party_[i];
        if (member && (member->flags_ & MapObjectFlag::Selected)) indexes.push_back(i);
    }
    return indexes;
}

void CBattleScreen::AttackUnit(CUnit* target, bool ctrl) {
    int kind = 0;
    while (kind < 6 && orderKeys_[kind] == 0) ++kind;

    std::vector<CUnit*> units;
    for (int i : SelectedIndexes()) {
        CUnit* member = party_[i];
        units.push_back(member);
        portraits_->Wobble(i, kWobbleNod);
        if (target->Level() - member->Level() > 5) Speech(member->id_, AckKind::BigAttack, nullptr);
    }
    if (!units.empty()) g_clientWorld.LocalView()->SendAttack(&units, target, kind, ctrl);
}

void CBattleScreen::OrderDeadUnit(CUnit* target, bool ctrl) {
    std::vector<CUnit*> units;
    for (int i : SelectedIndexes()) {
        units.push_back(party_[i]);
        portraits_->Wobble(i, kWobbleNod);
    }
    if (!units.empty()) g_clientWorld.LocalView()->SendUseDead(&units, target, ctrl);
}

// --- the pending spell or quick item used --------------------------------------------------------

void CBattleScreen::UseOnUnit(CUnit* target, bool quick, bool ctrl) {
    for (int i : SelectedIndexes()) {
        CUnit* member = party_[i];
        bool able;
        if (!quick) {
            g_clientWorld.LocalView()->SendCastOnUnit(pendingSlot_, member, target, ctrl);
            able = member->CanCastSpell(pendingSlot_);
        } else {
            g_clientWorld.LocalView()->SendItemOnUnit(pendingSlot_, member, target, ctrl);
            quickItems_->SetUnit(member);
            able = member->CanUseQuickItem(pendingSlot_);
        }
        if (able) portraits_->Wobble(i, kWobbleNod);
    }
    SetPendingAction(kPendingNone, -1, -1);
}

// The shift below stays for the later members.
void CBattleScreen::UseAtPoint(Vector3 point, CUnit* unit, bool quick) {
    for (int i : SelectedIndexes()) {
        CUnit* member = party_[i];
        if (member == unit) {
            Vector3 axis;
            unit->localRotation_.AxisY(&axis);
            const float k = 0.1f;
            point.x = point.x + -axis.x * k;
            point.y = point.y + -axis.y * k;
            point.z = point.z + -axis.z * k;
        }
        bool able;
        if (quick) {
            g_clientWorld.LocalView()->SendItemAtPoint(pendingSlot_, member, point, 0);
            quickItems_->SetUnit(member);
            able = member->CanUseQuickItem(pendingSlot_);
        } else {
            g_clientWorld.LocalView()->SendCastAtPoint(pendingSlot_, member, point, 0);
            able = member->CanCastSpell(pendingSlot_);
        }
        if (able) portraits_->Wobble(i, kWobbleNod);
    }
    SetPendingAction(kPendingNone, -1, -1);
}

// --- a unit's speech event -----------------------------------------------------------------------

void CBattleScreen::Speech(int id, AckKind kind, char* extra) {
    CUnit* unit = UnitWithId(g_clientWorld.objectManager_, id);
    if (!unit) return;
    const SpeechRecord* phrase = (kind == AckKind::Scenario || kind == AckKind::ScenarioTalking)
                                     ? acks::SayNamedAck(unit, extra, kind == AckKind::ScenarioTalking)
                                     : acks::SayAck(unit, kind);
    if (phrase) {
        // The phrase's text.
        std::string text;
        std::string line = UnitName(unit) + ":\n";
        CTextManager* texts = g_clientWorld.texts_;
        if (texts->Open(std::string("say ") + phrase->name.data())) {
            texts->ReadToHash(&text, 1);
            str::TrimRight(text, "\r\n");
            text += "\n";
            // Both go to the top panel's page 0 and, while it shows that page, the message log.
            auto post = [this](const char* part) {
                topPanel_->pageTexts_[0] += part;
                if (topPanel_->page_ == 0) messageLog_->AddText(part);
            };
            post(line.c_str());
            post(text.c_str());
        }
    }

    auto* party = &party_;
    auto unitAt = [party](int i) { return (*party)[i]; };
    if (kind == AckKind::Kill) {
        // The member's timed face (no null test).
        for (int i = 0; i < std::ssize(*party); ++i) {
            if (unitAt(i)->id_ != id) continue;
            HudPortraitSlot* slots = portraits_->slots_.data();
            slots[i].faceTimer_ = 0.0f;
            portraits_->slots_[i].faceMode_ = 1;
        }
    } else if (kind >= AckKind::NoWayToAttack && kind <= AckKind::Injured) {
        for (int i = 0; i < std::ssize(*party); ++i) {
            if (unitAt(i)->id_ == id) portraits_->Wobble(i, kWobbleShake);
        }
    }
}
