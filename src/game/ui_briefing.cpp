// The briefing / base screen's main virtuals (CBriefingScreen, ui_briefing.h): close, message
// box answers, a unit leaving the party, game over, the "briefing" command, the chat window and the
// saved state. See docs/notes/ui.md, "Briefing / base screen".
#include "ui.h"
#include "options.h"
#include "ui_briefing.h"

#include "camera.h"
#include "core_streams.h"
#include "object_tables.h"
#include "unit.h"

#include "lib/crt.h"
#include "text_split.h"
#include "party.h"
#include "world.h"
#include "world_glue.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <bit>
#include <cstring>
#include <new>
#include <string>

namespace {

// The map edge fade (a float the zone screens set while they are open).

constexpr std::uint32_t kLeaveZoneYes = 0x10001;
constexpr std::uint32_t kGameOverLoad = 0x20001;
constexpr std::uint32_t kGameOverQuit = 0x20002;
constexpr std::uint32_t kLeaveAnswer = 0x90001;
constexpr std::uint32_t kLoadWindowClosed = 0x100001;
constexpr int kGameOverResult = 0x20000;

int RecordUnitId(const PartyUnitRecord& record) { return static_cast<int>(record.unitId); }

const char* ZoneTopicFormat() { return "b.%s"; }
constexpr std::uint32_t kOneBits = 0x3F800000;   // 1.0f
constexpr int kTopicNameParts = 3;

} // namespace

// --- The variable copies ------------------------------------------------------------------------

BriefingVariables::BriefingVariables(const char* prefix) { PlayerZeroVariables(prefix, &table_); }

BriefingVariables::~BriefingVariables() = default;

void CBriefingScreen::Close() {
    options::g_options->borderFogDistance_ = savedEdgeFade_;
    GameStopSounds(1);
    GameStopAmbience();
    std::uint32_t& flags = g_camera->flags_;
    flags = (flags & ~(CCamera::kBounded | CCamera::kAlternate)) | CCamera::kFree;
}

void CBriefingScreen::OnMessageResult(std::uint32_t result) {
    switch (result) {
    case kLeaveZoneYes:
        GameLeaveThroughExit(exit_);
        break;
    case kGameOverLoad: {
        interface_->PushWindow(new CSaveLoadWindow(interface_, false, false), 1, 1);
        break;
    }
    case kGameOverQuit:
    case kLeaveAnswer:
    case kLoadWindowClosed:
        interface_->closeAll_ = true;
        g_gameFlow->DropSessions();
        uiorig::LeaveGame();
        break;
    default:
        break;
    }
    if (IsMultiplayerClient()) PlayersChanged(1);
}

void CBriefingScreen::OnUnitJoined(CUnit* unit) {
    CParty* player = LocalPlayerData();
    int count = std::ssize(player->members_);
    if (count == 0) return;
    int place = 0;
    int record = 0;
    for (; record < count; ++record) {
        int id = RecordUnitId(player->members_[record]);
        if (unit->id_ == id) break;
        if (place < std::ssize(party_) && party_[place]->id_ == id) ++place;
    }
    if (record < count) {
        // Insert `unit` at `place` (the slot is zeroed, then written); the
        // original then looks the unit up in the server world without using the result.
        int size = std::ssize(party_);
        if (place >= size) {
            party_.resize(place + 1);
        } else {
            party_.resize(size + 1);
            std::memmove(&party_[place + 1], &party_[place], (size - place) * sizeof(party_[0]));
        }
        party_[place] = unit;
    } else {
        // SetAtGrow(size, unit).
        int size = std::ssize(party_);
        party_.resize(size + 1);
        party_[size] = unit;
    }
    if (unit->id_ != RecordUnitId(player->members_[0])) return;

    leader_ = unit;
    g_clientWorld->Select(unit);
    g_camera->SetTarget(unit);
    MapRecord* zone = g_mapDatabase->FindMap(LocalPlayerZone());
    std::string prefix;
    prefix = str::Format(ZoneTopicFormat(), zone->name);
    BriefingVariables variables(prefix.c_str());
    std::vector<std::string> parts;
    for (auto& node : variables) {
        std::uint32_t bits;
        bits = std::bit_cast<std::uint32_t>(node.second);
        if (bits != kOneBits) continue;
        text::Split(node.first, &parts, '.');
        if (parts.size() != kTopicNameParts) continue;
        topic_ = parts[2];
        briefingStarted_ = true;
        StartDialogue(1, 1);
        break;
    }
}

void CBriefingScreen::OnUnitLeft(CUnit* unit) {
    if (unit == leader_) leader_ = nullptr;
    int count = std::ssize(party_);
    int index = 0;
    while (index < count && party_[index]->id_ != unit->id_) ++index;
    if (index >= count) return;
    int after = count - index - 1;
    if (after != 0) std::memmove(&party_[index], &party_[index + 1], after * sizeof(party_[0]));
    party_.pop_back();
}

void CBriefingScreen::OnGameOver() {
    interface_->PlaySound("buttons\\gameover.wav", 0);
    CMessageBox* box = new CMessageBox(interface_, "game_over", "game_over_msg", kGameOverResult, 3, 1);
    interface_->PushWindow(box, 1, 1);
}

void CBriefingScreen::OnBriefing(std::string_view name) {
    topic_ = std::string(name);
    StartDialogue(1, 1);
}

void CBriefingScreen::ShowChat(char show) {
    if (IsMultiplayerClient() && show && chat_) chat_->ShowPlayers();
}

void CBriefingScreen::SaveState() {
    int zero = 0;
    g_saveStream->WriteValue(zero);
}

void CBriefingScreen::LoadPersonTexts() { ::LoadPersonTexts(&personTexts_); }
