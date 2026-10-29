// The battle screen's main virtuals for closing, the first poll, picked-up items, a unit leaving
// the party and the saved state (CBattleScreen, ui.h). See docs/notes/ui.md, section 6.
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
bool AutoSaveEnabled() { return options::g_options->autosave_ != 0; }
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

void CBattleScreen::CloseScreen() {
    g_camera.flags_ |= CCamera::kFree;
    worlddata::g_updatePeriod = kDefaultUpdatePeriod;
    g_cursor.SetHeld(0);
    GameStopSounds(1);
    GameStopAmbience();
    party_.clear();
}

bool CBattleScreen::ReadyToStopPolling() {
    if (hasHero_) {
        auto& party = party_;
        if (!party.empty()) g_camera.SetTarget(party[0]);
        if (AutoSaveEnabled()) ::QuickSave(AutoSaveName(), true);
        hasHero_ = false;
        interface_->OpenHelp(false);
    }
    return true;
}

void CBattleScreen::ItemsPicked(std::list<CItem*>* items, int money, bool crippled) {
    for (CItem* item : *items) topPanel_->AddPickedUp(item, crippled);
    topPanel_->AddMoney(money);
    topPanel_->ShowItems();
    RefreshSelection();
}

void CBattleScreen::UnitLeft(CUnit* unit) {
    if (IsMultiplayerClient()) {
        topPanel_->pageTexts_[0].clear();
        if (topPanel_->page_ == 0) messageLog_->SetText(game::kEmptyText);
        return;
    }
    auto& party = party_;
    int index = 0;
    while (index < std::ssize(party) && party[index] != unit) ++index;
    if (index >= std::ssize(party)) return;
    int after = std::ssize(party) - index - 1;
    if (after != 0) std::memmove(&party[index], &party[index + 1], after * sizeof(party[0]));
    party.pop_back();
    portraits_->Rebuild(false);
    RefreshSelection();
}

void CBattleScreen::SaveState() {
    CStream* stream = &g_saveStream;
    auto& party = party_;
    // The ids go through an array first; the bytes written are the same.
    int count = 0;
    for (int i = 0; i < std::ssize(party); ++i)
        if (party[i]->flags_ & MapObjectFlag::Selected) ++count;
    stream->WriteValue(count);
    for (int i = 0; i < std::ssize(party); ++i) {
        auto* unit = party[i];
        if (unit->flags_ & MapObjectFlag::Selected) stream->WriteValue(unit->id_);
    }
    stream->WriteValue(map_->zoom_);
    stream->WriteValue(interface_->serverFlag_);
    stream->WriteValue(LocalPlayerData()->money_);   // the party's +0x50 (Create reads it back and drops it)
    stream->WriteString(topPanel_->pageTexts_[0]);
    stream->WriteValue(exitPending_);
    stream->WriteValue(pendingExit_);
}

void CBattleScreen::MarkPortrait(int index) {
    auto* portraits = portraits_;
    portraits->SceneMesh(index * kPortraitSprites + 4)->visible_ = true;
    const Vector3 axis = {-1.0f, 0.0f, 0.0f};
    Quaternion turn;
    portraits->slots_[index].restB_ = *turn.SetAxisAngle(axis, SelectedFaceAngle());
}

void CBattleScreen::UnitJoined(CUnit* unit) {
    if (IsMultiplayerClient()) {
        topPanel_->pageTexts_[0].clear();
        if (topPanel_->page_ == 0) messageLog_->SetText(game::kEmptyText);
        return;
    }
    CParty* player = LocalPlayerData();
    auto& party = party_;
    // The unit's place: after the party units whose records come before its record.
    int place = 0;
    int record = 0;
    bool found = false;
    for (; record < std::ssize(player->members_); ++record) {
        int id = static_cast<int>(player->members_[record].unitId);
        if (unit->id_ == id) {
            found = true;
            break;
        }
        if (place < std::ssize(party) && party[place]->id_ == id) ++place;
    }
    bool selected = false;
    if (found) {
        int count = std::ssize(party);
        if (place >= count) {
            party.resize(place + 1);
        } else {
            party.resize(count + 1);
            std::memmove(&party[place + 1], &party[place], (count - place) * sizeof(party[0]));
        }
        party[place] = unit;
        selected = player->members_[record].selected != 0;
    } else {
        int count = std::ssize(party);
        party.resize(count + 1);
        party[count] = unit;
    }
    portraits_->Rebuild(false);
    if (selected) {
        g_clientWorld.Select(unit);
        MarkPortrait(place);
    }
    // A hero the save had selected.
    for (int i = 0; i < std::ssize(heroes_); ++i) {
        if (static_cast<int>(heroes_[i]) != unit->id_) continue;
        int after = std::ssize(heroes_) - i - 1;
        if (after != 0) std::memmove(&heroes_[i], &heroes_[i + 1], after * sizeof(heroes_[0]));
        heroes_.pop_back();
        g_clientWorld.Select(unit);
        MarkPortrait(place);
        break;
    }
    RefreshSelection();
}

void CBattleScreen::MessageResult(int result) {
    switch (static_cast<std::uint32_t>(result)) {
    case kLeaveZoneYes:
        GameLeaveThroughExit(pendingExit_);
        break;
    case kGameOverLoad: {
        interface_->PushWindow(new CSaveLoadWindow(interface_, false, false), true, true);
        break;
    }
    case kGameOverQuit:
    case kLeaveAnswer:
    case kLoadWindowClosed:
        interface_->closeAll_ = true;
        g_gameFlow.DropSessions();
        g_gameFlow.CloseSessions();
        break;
    default:
        break;
    }
}

void CBattleScreen::GameOver(bool won) {
    GameStopSounds(1);
    GameStopAmbience();
    CMessageBox* box;
    if (!won) {
        interface_->PlaySound("buttons\\gameover.wav");
        box = new CMessageBox(interface_, "game_over", "game_over_msg", 0x20000, 3, 1);
    } else {
        box = new CMessageBox(interface_, "end_of_game", "end_of_game_msg", 0xFFFF0000u, 3, 1);
    }
    interface_->PushWindow(box, true, true);
}
