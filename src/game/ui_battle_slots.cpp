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

constexpr std::uint32_t kUnitSelected = 0x10;   // CEngineObject::flags_ of a selected party unit

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

CHudTopPanel* TopPanel(CUiWindow* panel) { return static_cast<CHudTopPanel*>(panel); }
CHudMap* Map(CUiWindow* map) { return static_cast<CHudMap*>(map); }

} // namespace

void CBattleScreen::Close() {
    g_camera->flags_ |= CCamera::kFree;
    worlddata::g_updatePeriod = kDefaultUpdatePeriod;
    g_cursor->SetHeld(0);
    GameStopSounds(1);
    GameStopAmbience();
    party_.clear();
}

bool CBattleScreen::PollShown() {
    if (hasHero_) {
        auto& party = party_;
        if (!party.empty()) g_camera->SetTarget(party[0]);
        if (AutoSaveEnabled()) ::QuickSave(AutoSaveName(), static_cast<char>(1));
        hasHero_ = false;
        interface_->OpenHelp(0);
    }
    return true;
}

void CBattleScreen::OnItemsPicked(std::list<CItem*>* items, int money, int crippled) {
    for (auto node = items->begin(); node != items->end();) {
        auto next = std::next(node);
        topPanel_->AddPickedUp(*node, static_cast<char>(crippled));
        node = next;
    }
    topPanel_->AddMoney(money);
    topPanel_->ShowItems();
    RefreshSelection();
}

void CBattleScreen::OnUnitLeft(CUnit* unit) {
    if (IsMultiplayerClient()) {
        CHudTopPanel* panel = TopPanel(topPanel_);
        panel->pageTexts_[0].clear();
        if (panel->page_ == 0)
            panel->owner_->MessageLog()->SetText(game::kEmptyText);
        return;
    }
    auto& party = party_;
    int index = 0;
    while (index < std::ssize(party) && party[index] != unit) ++index;
    if (index >= std::ssize(party)) return;
    int after = std::ssize(party) - index - 1;
    if (after != 0) std::memmove(&party[index], &party[index + 1], after * sizeof(party[0]));
    party.pop_back();
    portraits_->Rebuild(0);
    RefreshSelection();
}

void CBattleScreen::SaveState() {
    CStream* stream = &g_saveStream.Get();
    auto& party = party_;
    // The ids go through an array first; the bytes written are the same.
    int count = 0;
    for (int i = 0; i < std::ssize(party); ++i)
        if (party[i]->flags_ & kUnitSelected) ++count;
    stream->Write(&count, 4);
    for (int i = 0; i < std::ssize(party); ++i) {
        auto* unit = party[i];
        if (unit->flags_ & kUnitSelected) stream->Write(&unit->id_, 4);
    }
    stream->Write(&Map(map_)->zoom_, 4);
    stream->Write(&interface_->serverFlag_, 1);
    stream->Write(&LocalPlayerData()->money_, 4);   // the party's +0x50 (Create reads it back and drops it)
    stream->WriteString(TopPanel(topPanel_)->pageTexts_[0]);
    stream->Write(&exitPending_, 1);
    stream->Write(&pendingExit_, 4);
}

void CBattleScreen::MarkPortrait(int index) {
    auto* portraits = portraits_;
    portraits->SceneMesh(index * kPortraitSprites + 4)->visible_ = 1;
    const Vector3 axis = {-1.0f, 0.0f, 0.0f};
    Quaternion turn;
    portraits->slots_[index].restB_ = *turn.SetAxisAngle(axis, SelectedFaceAngle());
}

void CBattleScreen::OnUnitJoined(CUnit* unit) {
    if (IsMultiplayerClient()) {
        CHudTopPanel* panel = TopPanel(topPanel_);
        panel->pageTexts_[0].clear();
        if (panel->page_ == 0)
            panel->owner_->MessageLog()->SetText(game::kEmptyText);
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
    portraits_->Rebuild(0);
    if (selected) {
        g_clientWorld->Select(unit);
        MarkPortrait(place);
    }
    // A hero the save had selected.
    for (int i = 0; i < std::ssize(heroes_); ++i) {
        if (static_cast<int>(heroes_[i]) != unit->id_) continue;
        int after = std::ssize(heroes_) - i - 1;
        if (after != 0) std::memmove(&heroes_[i], &heroes_[i + 1], after * sizeof(heroes_[0]));
        heroes_.pop_back();
        g_clientWorld->Select(unit);
        MarkPortrait(place);
        break;
    }
    RefreshSelection();
}

void CBattleScreen::OnMessageResult(int result) {
    switch (static_cast<std::uint32_t>(result)) {
    case kLeaveZoneYes:
        GameLeaveThroughExit(pendingExit_);
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
}

void CBattleScreen::OnGameOver(char won) {
    GameStopSounds(1);
    GameStopAmbience();
    CMessageBox* box;
    if (!won) {
        interface_->PlaySound("buttons\\gameover.wav", 0);
        box = new CMessageBox(interface_, "game_over", "game_over_msg", 0x20000, 3, 1);
    } else {
        box = new CMessageBox(interface_, "end_of_game", "end_of_game_msg", 0xFFFF0000u, 3, 1);
    }
    interface_->PushWindow(box, 1, 1);
}
