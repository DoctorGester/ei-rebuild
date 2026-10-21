// The battle screen's main-vtable slots for closing, the first poll, picked-up items, a unit leaving
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

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "world_glue.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstring>

namespace {

// The world's update period in milliseconds (the harness's `speed`; the game's default 55).
int& WorldUpdatePeriod() { return ei::GameData<int>(0x0078FF34); }
constexpr int kDefaultUpdatePeriod = 55;
// The autosave option (nonzero: save "savea" on entering a zone).
bool AutoSaveEnabled() { return options::TheOptions()->autosave_ != 0; }
const char* AutoSaveName() { return "savea"; }

constexpr std::uint32_t kUnitSelected = 0x10;   // CEngineObject::flags_ of a selected party unit
constexpr int kMessageLogChild = 9;


// The portraits' selected-face turn: 007C8144 (pi) times 0073CDCC (1/7).
float SelectedFaceAngle() {
    return ei::GameData<const volatile float>(0x007C8144) * 0.14285715f /*0073CDCC*/;
}

// The load window the game-over box opens (0x194 bytes, built inline).
void** SaveLoadVtable() { return ei::GamePointer<void*>(0x00741388); }
void** SaveLoadInputVtable() { return ei::GamePointer<void*>(0x00741354); }

constexpr std::uint32_t kLeaveZoneYes = 0x10001;
constexpr std::uint32_t kGameOverLoad = 0x20001;
constexpr std::uint32_t kGameOverQuit = 0x20002;
constexpr std::uint32_t kLeaveAnswer = 0x90001;
constexpr std::uint32_t kLoadWindowClosed = 0x100001;
constexpr int kPortraitSprites = 5;   // sprites per portrait; the fifth (4) is the selection marker

CHudTopPanel* TopPanel(CUiWindow* panel) { return static_cast<CHudTopPanel*>(panel); }
CHudMap* Map(CUiWindow* map) { return static_cast<CHudMap*>(map); }

} // namespace

// --- 00635E60: slot 26 --------------------------------------------------------------------------

void CBattleScreen::Close() {
    TheCamera()->flags_ |= CCamera::kFree;
    WorldUpdatePeriod() = kDefaultUpdatePeriod;
    TheCursor()->SetHeld(0);
    GameStopSounds(1);
    GameStopAmbience();
    party_.SetSize(0, -1);
}
EI_REPLACE_METHOD(0x00635E60, CBattleScreen::Close);

// --- 006360F0: slot 35 --------------------------------------------------------------------------

bool CBattleScreen::PollShown() {
    if (hasHero_) {
        auto& party = party_;
        if (party.size > 0) TheCamera()->SetTarget(party.data[0]);
        if (AutoSaveEnabled()) AutoSaveOriginal(AutoSaveName(), 1);
        hasHero_ = false;
        interface_->OpenHelp(0);
    }
    return true;
}
EI_REPLACE_METHOD(0x006360F0, CBattleScreen::PollShown);

// --- 00636150: slot 28 --------------------------------------------------------------------------

void CBattleScreen::OnItemsPicked(mfc::CTypedPtrList<CItem>* items, int money, int crippled) {
    for (auto* node = items->head; node != nullptr;) {
        auto* next = node->next;
        topPanel_->AddPickedUp(node->data, static_cast<char>(crippled));
        node = next;
    }
    topPanel_->AddMoney(money);
    TopPanelShowPageOriginal(topPanel_);
    RefreshSelection();
}
EI_REPLACE_METHOD(0x00636150, CBattleScreen::OnItemsPicked);

// --- 006364E0: slot 30 --------------------------------------------------------------------------

void CBattleScreen::OnUnitLeft(CUnit* unit) {
    if (IsMultiplayerClient()) {
        CHudTopPanel* panel = TopPanel(topPanel_);
        panel->pageTexts_[0].Empty();
        if (panel->page_ == 0)
            static_cast<CUiScrollText*>(panel->owner_->children_.data[kMessageLogChild])->SetText(game::EmptyText());
        return;
    }
    auto& party = party_;
    int index = 0;
    while (index < party.size && party.data[index] != unit) ++index;
    if (index >= party.size) return;
    int after = party.size - index - 1;
    if (after != 0) std::memmove(&party.data[index], &party.data[index + 1], after * sizeof(party.data[0]));
    --party.size;
    PortraitsRebuildOriginal(portraits_, nullptr, 0);
    RefreshSelection();
}
EI_REPLACE_METHOD(0x006364E0, CBattleScreen::OnUnitLeft);

// --- 00635080: slot 40 --------------------------------------------------------------------------

void CBattleScreen::SaveState() {
    CStream* stream = GameSaveStream();
    auto& party = party_;
    // The original collects the ids into a CDWordArray first; the bytes written are the same.
    int count = 0;
    for (int i = 0; i < party.size; ++i)
        if (party.data[i]->flags_ & kUnitSelected) ++count;
    stream->Write(&count, 4);
    for (int i = 0; i < party.size; ++i) {
        auto* unit = party.data[i];
        if (unit->flags_ & kUnitSelected) stream->Write(&unit->id_, 4);
    }
    stream->Write(&Map(map_)->zoom_, 4);
    stream->Write(&interface_->serverFlag_, 1);
    stream->Write(&LocalPlayerData()->money_, 4);   // the party's +0x50 (Create reads it back and drops it)
    const char* text = TopPanel(topPanel_)->pageTexts_[0];
    stream->Write(text, static_cast<std::uint32_t>(std::strlen(text) + 1));
    stream->Write(&exitPending_, 1);
    stream->Write(&pendingExit_, 4);
}
EI_REPLACE_METHOD(0x00635080, CBattleScreen::SaveState);

// --- 006361B0: slot 29 --------------------------------------------------------------------------

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
        panel->pageTexts_[0].Empty();
        if (panel->page_ == 0)
            static_cast<CUiScrollText*>(panel->owner_->children_.data[kMessageLogChild])->SetText(game::EmptyText());
        return;
    }
    CParty* player = LocalPlayerData();
    auto& party = party_;
    // The unit's place: after the party units whose records come before its record.
    int place = 0;
    int record = 0;
    bool found = false;
    for (; record < player->members_.size; ++record) {
        int id = static_cast<int>(player->members_.data[record].unitId);
        if (unit->id_ == id) {
            found = true;
            break;
        }
        if (place < party.size && party.data[place]->id_ == id) ++place;
    }
    bool selected = false;
    if (found) {
        int count = party.size;
        if (place >= count) {
            party.SetSize(place + 1, -1);
        } else {
            party.SetSize(count + 1, -1);
            std::memmove(&party.data[place + 1], &party.data[place], (count - place) * sizeof(party.data[0]));
        }
        party.data[place] = unit;
        selected = player->members_.data[record].selected != 0;
    } else {
        int count = party.size;
        party.SetSize(count + 1, -1);
        party.data[count] = unit;
    }
    PortraitsRebuildOriginal(portraits_, nullptr, 0);
    if (selected) {
        TheClientWorld()->Select(unit);
        MarkPortrait(place);
    }
    // A hero the save had selected.
    for (int i = 0; i < heroes_.size; ++i) {
        if (static_cast<int>(heroes_.data[i]) != unit->id_) continue;
        int after = heroes_.size - i - 1;
        if (after != 0) std::memmove(&heroes_.data[i], &heroes_.data[i + 1], after * sizeof(heroes_.data[0]));
        --heroes_.size;
        TheClientWorld()->Select(unit);
        MarkPortrait(place);
        break;
    }
    RefreshSelection();
}
EI_REPLACE_METHOD(0x006361B0, CBattleScreen::OnUnitJoined);

// --- 00635EB0: slot 27 --------------------------------------------------------------------------

void CBattleScreen::OnMessageResult(int result) {
    switch (static_cast<std::uint32_t>(result)) {
    case kLeaveZoneYes:
        GameLeaveThroughExit(pendingExit_);
        break;
    case kGameOverLoad: {
        auto* window = static_cast<CSaveLoadWindow*>(gamecrt::operator_new(sizeof(CSaveLoadWindow)));
        if (window) {
            window->CUiContainer::Construct(interface_);
            window->entriesBegin_ = nullptr;
            window->entriesEnd_ = nullptr;
            window->entriesCapacity_ = nullptr;
            window->isSave_ = false;
            window->sliding_ = false;
            new (static_cast<void*>(&window->name_)) mfc::CString();
            window->vtable_ = SaveLoadVtable();
            window->inputVtable_ = SaveLoadInputVtable();
        }
        // (Like the original, the flags are written again without checking the allocation.)
        window->isSave_ = false;
        window->sliding_ = false;
        interface_->PushWindow(window, 1, 1);
        break;
    }
    case kGameOverQuit:
    case kLeaveAnswer:
    case kLoadWindowClosed:
        interface_->closeAll_ = true;
        TheGameFlow()->DropSessions();
        uiorig::LeaveGame();
        break;
    default:
        break;
    }
}
EI_REPLACE_METHOD(0x00635EB0, CBattleScreen::OnMessageResult);

// --- 006365E0: slot 31 --------------------------------------------------------------------------

void CBattleScreen::OnGameOver(char won) {
    GameStopSounds(1);
    GameStopAmbience();
    void* box;
    if (!won) {
        interface_->PlaySound("buttons\\gameover.wav", 0);
        void* memory = gamecrt::operator_new(sizeof(CMessageBox));
        box = memory ? static_cast<CMessageBox*>(memory)->Construct(interface_, "game_over",
                                                   "game_over_msg", 0x20000, 3, 1)
                     : nullptr;
    } else {
        CMessageBox* memory = gamecrt::operator_new_as<CMessageBox>();
        box = memory ? memory->Construct(interface_, "end_of_game",
                                                   "end_of_game_msg", 0xFFFF0000u, 3, 1)
                     : nullptr;
    }
    interface_->PushWindow(static_cast<CUiWindow*>(box), 1, 1);
}
EI_REPLACE_METHOD(0x006365E0, CBattleScreen::OnGameOver);
