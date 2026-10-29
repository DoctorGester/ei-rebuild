// The battle screen's main virtuals for closing, the first poll, picked-up items, a unit leaving
// the party and the saved state (BattleScreen, ui.h). See docs/notes/ui.md, section 6.
#include "ui.h"
#include "options.h"
#include "ui_camp.h"

#include "camera.h"
#include "maths.h"
#include "ui_cursor.h"
#include "core_streams.h"
#include "game_strings.h"
#include "party.h"
#include "unit.h"
#include "world.h"

#include "lib/crt.h"
#include <cstdint>
#include <cstring>
#include "world_glue.h"
#include "game_singletons.h"
#include "world_data.h"

#include <list>

namespace {

// The world's update period in milliseconds (the harness's `speed`; the game's default 55).
constexpr int kDefaultUpdatePeriod = 55;
// The autosave option (nonzero: save "savea" on entering a zone).
bool AutoSaveEnabled() { return options::g_options.autosave != 0; }
const char* AutoSaveName() { return "savea"; }


// The portraits' selected-face turn: pi / 2 times (1/7).
float SelectedFaceAngle() {
    return kHudQuarterTurn * 0.14285715f;
}

constexpr std::uint32_t kLeaveZoneYes = 0x10001;
constexpr std::uint32_t kGameOverLoad = 0x20001;
constexpr std::uint32_t kGameOverQuit = 0x20002;
constexpr std::uint32_t kLeaveAnswer = 0x90001;
constexpr std::uint32_t kLoadWindowClosed = 0x100001;
constexpr int kPortraitSprites = 5;   // sprites per portrait; the fifth (4) is the selection marker

} // namespace

void BattleScreen::CloseScreen() {
    g_camera.flags |= Camera::kFree;
    worlddata::g_updatePeriod = kDefaultUpdatePeriod;
    g_cursor.SetHeld(0);
    GameStopSounds(1);
    GameStopAmbience();
    party.clear();
}

bool BattleScreen::ReadyToStopPolling() {
    if (hasHero) {
        auto& currentParty = party;
        if (!currentParty.empty()) g_camera.SetTarget(currentParty[0]);
        if (AutoSaveEnabled()) ::QuickSave(AutoSaveName(), true);
        hasHero = false;
        ui->OpenHelp(false);
    }
    return true;
}

void BattleScreen::ItemsPicked(std::list<Item*>* items, int money, bool crippled) {
    for (Item* item : *items) topPanel->AddPickedUp(item, crippled);
    topPanel->AddMoney(money);
    topPanel->ShowItems();
    RefreshSelection();
}

void BattleScreen::UnitLeft(CUnit* unit) {
    if (IsMultiplayerClient()) {
        topPanel->pageTexts[0].clear();
        if (topPanel->page == 0) messageLog->SetText(game::kEmptyText);
        return;
    }
    auto& currentParty = party;
    int index = 0;
    while (index < std::ssize(currentParty) && currentParty[index] != unit) ++index;
    if (index >= std::ssize(currentParty)) return;
    int after = std::ssize(currentParty) - index - 1;
    if (after != 0) std::memmove(&currentParty[index], &currentParty[index + 1], after * sizeof(currentParty[0]));
    currentParty.pop_back();
    portraits->Rebuild(false);
    RefreshSelection();
}

void BattleScreen::SaveState() {
    ByteStream* stream = &g_saveStream;
    auto& currentParty = party;
    // The ids go through an array first; the bytes written are the same.
    int count = 0;
    for (int i = 0; i < std::ssize(currentParty); ++i)
        if (currentParty[i]->flags & MapObjectFlag::Selected) ++count;
    stream->WriteValue(count);
    for (int i = 0; i < std::ssize(currentParty); ++i) {
        auto* unit = currentParty[i];
        if (unit->flags & MapObjectFlag::Selected) stream->WriteValue(unit->id);
    }
    stream->WriteValue(map->zoom);
    stream->WriteValue(ui->serverFlag);
    stream->WriteValue(LocalPlayerData()->money);    // the party's +0x50 (Create reads it back and drops it)
    stream->WriteString(topPanel->pageTexts[0]);
    stream->WriteValue(exitPending);
    stream->WriteValue(pendingExit);
}

void BattleScreen::MarkPortrait(int index) {
    auto* currentPortraits = portraits;
    currentPortraits->SceneMesh(index * kPortraitSprites + 4)->visible = true;
    const Vector3 axis = {-1.0f, 0.0f, 0.0f};
    Quaternion turn;
    currentPortraits->slots[index].restB = *turn.SetAxisAngle(axis, SelectedFaceAngle());
}

void BattleScreen::UnitJoined(CUnit* unit) {
    if (IsMultiplayerClient()) {
        topPanel->pageTexts[0].clear();
        if (topPanel->page == 0) messageLog->SetText(game::kEmptyText);
        return;
    }
    CParty* player = LocalPlayerData();
    auto& currentParty = party;
    // The unit's place: after the party units whose records come before its record.
    int place = 0;
    int record = 0;
    bool found = false;
    for (; record < std::ssize(player->members); ++record) {
        int unitId = static_cast<int>(player->members[record].unitId);
        if (unit->id == unitId) {
            found = true;
            break;
        }
        if (place < std::ssize(currentParty) && currentParty[place]->id == unitId) ++place;
    }
    bool selected = false;
    if (found) {
        int count = std::ssize(currentParty);
        if (place >= count) {
            currentParty.resize(place + 1);
        } else {
            currentParty.resize(count + 1);
            std::memmove(&currentParty[place + 1], &currentParty[place], (count - place) * sizeof(currentParty[0]));
        }
        currentParty[place] = unit;
        selected = player->members[record].selected != 0;
    } else {
        int count = std::ssize(currentParty);
        currentParty.resize(count + 1);
        currentParty[count] = unit;
    }
    portraits->Rebuild(false);
    if (selected) {
        g_clientWorld.Select(unit);
        MarkPortrait(place);
    }
    // A hero the save had selected.
    for (int i = 0; i < std::ssize(heroes); ++i) {
        if (static_cast<int>(heroes[i]) != unit->id) continue;
        int after = std::ssize(heroes) - i - 1;
        if (after != 0) std::memmove(&heroes[i], &heroes[i + 1], after * sizeof(heroes[0]));
        heroes.pop_back();
        g_clientWorld.Select(unit);
        MarkPortrait(place);
        break;
    }
    RefreshSelection();
}

void BattleScreen::MessageResult(int result) {
    switch (static_cast<std::uint32_t>(result)) {
    case kLeaveZoneYes:
        GameLeaveThroughExit(pendingExit);
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
}

void BattleScreen::GameOver(bool won) {
    GameStopSounds(1);
    GameStopAmbience();
    MessageWindow* box;
    if (!won) {
        ui->PlaySound("buttons\\gameover.wav");
        box = new MessageWindow(ui, "game_over", "game_over_msg", 0x20000, 3, 1);
    } else {
        box = new MessageWindow(ui, "end_of_game", "end_of_game_msg", 0xFFFF0000u, 3, 1);
    }
    ui->PushWindow(box, true, true);
}
