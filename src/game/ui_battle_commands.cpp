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
#include "lib/containers.h"
#include "lib/str.h"

#include <vector>

namespace {

// HudPortraitSlot::wobbleKind.
constexpr int kWobbleNod = 1;
constexpr int kWobbleShake = 2;

} // namespace

// --- a party member toggled ----------------------------------------------------------------------

void BattleScreen::ToggleMember(int index) {
    auto* currentParty = &party;
    auto unitAt = [currentParty](int i) { return (*currentParty)[i]; };
    if (IsMultiplayerClient()) return;
    if (index >= std::ssize(*currentParty)) return;

    std::vector<std::uint32_t> chosen{};
    for (int i = 0; i < std::ssize(*currentParty); ++i) {
        Unit* member = unitAt(i);
        lib::SetAtGrow(chosen, lib::Count(chosen), member ? (member->flags & MapObjectFlag::Selected) : 0);
    }
    if (ui->shiftDown) {
        chosen[index] = chosen[index] == 0 ? 1u : 0u;
    } else {
        for (int i = 0; i < std::ssize(chosen); ++i)
            if (chosen[i] != 0) chosen[i] = 0;
        chosen[index] = 1;
    }
    g_clientWorld.Deselect();

    for (int i = 0; i < std::ssize(*currentParty); ++i) {
        auto* currentPortraits = portraits;
        std::uint32_t value = chosen[i];
        currentPortraits->SceneMesh(i * 5 + 4)->visible = value != 0;
        Quaternion turn;
        const Quaternion* rest = &kHudRestRotation;
        if (value != 0) {
            const Vector3 axis = {-1.0f, 0.0f, 0.0f};
            const float angle = kHudQuarterTurn *
                                0.14285715f;   // 1/7
            rest = turn.SetAxisAngle(axis, angle);
        }
        currentPortraits->slots[i].restB = *rest;
        Unit* member = unitAt(i);
        if (member && chosen[i] != 0) {
            g_clientWorld.Select(member);
            if (index == i) Speech(unitAt(i)->id, AckKind::Selected, nullptr);
        }
    }

    // Single player: the local player's unit records (0x784 bytes, +0x1C the unit id) keep the
    // members' selection, record i against member i.
    if (g_serverWorld.gameResult == 1) {
        if (auto* player = g_serverWorld.GetPlayer(0)) {
            for (int i = 0; i < std::ssize(player->GetParty()->members); ++i) {
                if (i >= std::ssize(*currentParty)) continue;
                PartyUnitRecord* record = &player->GetParty()->members[i];
                Unit* member = unitAt(i);
                if (member->id == static_cast<int>(record->unitId))    // the record's unit id
                    record->selected = static_cast<std::uint8_t>((member->flags >> 4) & 1);
            }
        }
    }
    RefreshSelection();
}

// --- the selected members ordered at a unit ------------------------------------------------------

std::vector<int> BattleScreen::SelectedIndexes() const {
    std::vector<int> indexes;
    for (int i = 0; i < std::ssize(party); ++i) {
        const Unit* member = party[i];
        if (member && (member->flags & MapObjectFlag::Selected)) indexes.push_back(i);
    }
    return indexes;
}

void BattleScreen::AttackUnit(Unit* target, bool ctrl) {
    int kind = 0;
    while (kind < 6 && orderKeys[kind] == 0) ++kind;

    std::vector<Unit*> units;
    for (int i : SelectedIndexes()) {
        Unit* member = party[i];
        units.push_back(member);
        portraits->Wobble(i, kWobbleNod);
        if (target->Level() - member->Level() > 5) Speech(member->id, AckKind::BigAttack, nullptr);
    }
    if (!units.empty()) g_clientWorld.LocalView()->SendAttack(&units, target, kind, ctrl);
}

void BattleScreen::OrderDeadUnit(Unit* target, bool ctrl) {
    std::vector<Unit*> units;
    for (int i : SelectedIndexes()) {
        units.push_back(party[i]);
        portraits->Wobble(i, kWobbleNod);
    }
    if (!units.empty()) g_clientWorld.LocalView()->SendUseDead(&units, target, ctrl);
}

// --- the pending spell or quick item used --------------------------------------------------------

void BattleScreen::UseOnUnit(Unit* target, bool quick, bool ctrl) {
    for (int i : SelectedIndexes()) {
        Unit* member = party[i];
        bool able;
        if (!quick) {
            g_clientWorld.LocalView()->SendCastOnUnit(pendingSlot, member, target, ctrl);
            able = member->CanCastSpell(pendingSlot);
        } else {
            g_clientWorld.LocalView()->SendItemOnUnit(pendingSlot, member, target, ctrl);
            quickItems->SetUnit(member);
            able = member->CanUseQuickItem(pendingSlot);
        }
        if (able) portraits->Wobble(i, kWobbleNod);
    }
    SetPendingAction(kPendingNone, -1, -1);
}

// The shift below stays for the later members.
void BattleScreen::UseAtPoint(Vector3 point, Unit* unit, bool quick) {
    for (int i : SelectedIndexes()) {
        Unit* member = party[i];
        if (member == unit) {
            Vector3 axis;
            unit->localRotation.AxisY(&axis);
            const float k = 0.1f;
            point.x = point.x + -axis.x * k;
            point.y = point.y + -axis.y * k;
            point.z = point.z + -axis.z * k;
        }
        bool able;
        if (quick) {
            g_clientWorld.LocalView()->SendItemAtPoint(pendingSlot, member, point, 0);
            quickItems->SetUnit(member);
            able = member->CanUseQuickItem(pendingSlot);
        } else {
            g_clientWorld.LocalView()->SendCastAtPoint(pendingSlot, member, point, 0);
            able = member->CanCastSpell(pendingSlot);
        }
        if (able) portraits->Wobble(i, kWobbleNod);
    }
    SetPendingAction(kPendingNone, -1, -1);
}

// --- a unit's speech event -----------------------------------------------------------------------

void BattleScreen::Speech(int unitId, AckKind kind, char* extra) {
    Unit* unit = UnitWithId(g_clientWorld.objectManager, unitId);
    if (!unit) return;
    const SpeechRecord* phrase = (kind == AckKind::Scenario || kind == AckKind::ScenarioTalking)
                                     ? acks::SayNamedAck(unit, extra, kind == AckKind::ScenarioTalking)
                                     : acks::SayAck(unit, kind);
    if (phrase) {
        // The phrase's text.
        std::string text;
        std::string line = UnitName(unit) + ":\n";
        TextManager* texts = g_clientWorld.texts;
        if (texts->Open(std::string("say ") + phrase->name.data())) {
            text = texts->ReadToHash();
            str::TrimRight(text, "\r\n");
            text += "\n";
            // Both go to the top panel's page 0 and, while it shows that page, the message log.
            auto post = [this](const char* part) {
                topPanel->pageTexts[0] += part;
                if (topPanel->page == 0) messageLog->AddText(part);
            };
            post(line.c_str());
            post(text.c_str());
        }
    }

    auto* currentParty = &party;
    auto unitAt = [currentParty](int i) { return (*currentParty)[i]; };
    if (kind == AckKind::Kill) {
        // The member's timed face (no null test).
        for (int i = 0; i < std::ssize(*currentParty); ++i) {
            if (unitAt(i)->id != unitId) continue;
            HudPortraitSlot* slots = portraits->slots.data();
            slots[i].faceTimer = 0.0f;
            portraits->slots[i].faceMode = 1;
        }
    } else if (kind >= AckKind::NoWayToAttack && kind <= AckKind::Injured) {
        for (int i = 0; i < std::ssize(*currentParty); ++i) {
            if (unitAt(i)->id == unitId) portraits->Wobble(i, kWobbleShake);
        }
    }
}
