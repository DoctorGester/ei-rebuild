// The briefing / base screen's main virtuals (BriefingScreen, ui_briefing.h): close, message
// box answers, a unit leaving the party, game over, the "briefing" command, the chat window and the
// saved state. See docs/notes/ui.md, "Briefing / base screen".
#include "ui_briefing.h"

#include "camera.h"
#include "core_streams.h"
#include "game_singletons.h"
#include "object_tables.h"
#include "options.h"
#include "party.h"
#include "text_split.h"
#include "ui.h"
#include "unit.h"
#include "world.h"
#include "world_glue.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/str.h"

#include <bit>
#include <cstring>
#include <new>
#include <string>

namespace {

constexpr int kLeaveZoneYes = 0x10001;
constexpr int kGameOverLoad = 0x20001;
constexpr int kGameOverQuit = 0x20002;
constexpr int kLeaveAnswer = 0x90001;
constexpr int kLoadWindowClosed = 0x100001;
constexpr int kGameOverResult = 0x20000;

constexpr const char* kZoneTopicFormat = "b.%s";
constexpr std::uint32_t kOneBits = 0x3F800000;   // 1.0f
constexpr int kTopicNameParts = 3;

} // namespace

void BriefingScreen::CloseScreen() {
    options::g_options.borderFogDistance = savedEdgeFade;
    GameStopSounds(1);
    GameStopAmbience();
    std::uint32_t& cameraFlags = g_camera.flags;
    cameraFlags = (cameraFlags & ~(Camera::kBounded | Camera::kAlternate)) | Camera::kFree;
}

void BriefingScreen::MessageResult(int result) {
    switch (result) {
    case kLeaveZoneYes:
        GameLeaveThroughExit(exit);
        break;
    case kGameOverLoad: {
        ui->PushWindow(new SaveLoadWindow(ui, false, false), true, true);
        break;
    }
    case kGameOverQuit:
    case kLeaveAnswer:
    case kLoadWindowClosed:
        ui->closeAll = true;
        g_gameFlow.DropSessions();
        g_gameFlow.CloseSessions();
        break;
    default:
        break;
    }
    if (IsMultiplayerClient()) PlayersChanged(1);
}

void BriefingScreen::UnitJoined(Unit* unit) {
    Party* player = LocalPlayerData();
    int count = lib::Count(player->members);
    if (count == 0) return;
    int place = 0;
    int record = 0;
    for (; record < count; ++record) {
        int unitId = static_cast<int>(player->members[record].unitId);
        if (unit->id == unitId) break;
        if (place < std::ssize(party) && party[place]->id == unitId) ++place;
    }
    if (record < count) {
        // Insert `unit` at `place` (the slot is zeroed, then written); the
        // original then looks the unit up in the server world without using the result.
        int size = lib::Count(party);
        if (place >= size) {
            party.resize(place + 1);
        } else {
            party.resize(size + 1);
            std::memmove(&party[place + 1], &party[place], (size - place) * sizeof(party[0]));
        }
        party[place] = unit;
    } else {
        // SetAtGrow(size, unit).
        int size = lib::Count(party);
        party.resize(size + 1);
        party[size] = unit;
    }
    if (unit->id != static_cast<int>(player->members[0].unitId)) return;

    leader = unit;
    g_clientWorld.Select(unit);
    g_camera.SetTarget(unit);
    MapRecord* zone = g_mapDatabase.FindMap(LocalPlayerZone());
    std::string prefix = str::Format(kZoneTopicFormat, zone->name);
    NamedFloatTable variables;
    PlayerZeroVariables(prefix.c_str(), &variables);
    std::vector<std::string> parts;
    for (auto& node : variables) {
        std::uint32_t bits;
        bits = std::bit_cast<std::uint32_t>(node.second);
        if (bits != kOneBits) continue;
        parts = text::Split(node.first, '.');
        if (parts.size() != kTopicNameParts) continue;
        topic = parts[2];
        briefingStarted = true;
        StartDialogue(true, true);
        break;
    }
}

void BriefingScreen::UnitLeft(Unit* unit) {
    if (unit == leader) leader = nullptr;
    int count = lib::Count(party);
    int index = 0;
    while (index < count && party[index]->id != unit->id) ++index;
    if (index >= count) return;
    int after = count - index - 1;
    if (after != 0) std::memmove(&party[index], &party[index + 1], after * sizeof(party[0]));
    party.pop_back();
}

void BriefingScreen::GameOver(bool) {
    ui->PlayInterfaceSound("buttons\\gameover.wav");
    MessageWindow* box = new MessageWindow(ui, "game_over", "game_over_msg", kGameOverResult, 3, 1);
    ui->PushWindow(box, true, true);
}

void BriefingScreen::ShowBriefing(std::string_view name) {
    topic = std::string(name);
    StartDialogue(true, true);
}

void BriefingScreen::PlayersChanged(int show) {
    if (IsMultiplayerClient() && show && chat) chat->ShowPlayers();
}

void BriefingScreen::SaveState() {
    int zero = 0;
    g_saveStream.WriteValue(zero);
}
