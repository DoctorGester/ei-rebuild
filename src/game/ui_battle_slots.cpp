// The battle screen's main-vtable slots for closing, the first poll, picked-up items, a unit leaving
// the party and the saved state (CBattleScreenSetup, ui_camp.h). See docs/notes/ui.md, section 6.
#include "ui_camp.h"

#include "camera.h"
#include "ui_cursor.h"
#include "core_streams.h"
#include "game_strings.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_ui.h"
#include "originals_world.h"

#include <cstring>

namespace {

// The world's update period in milliseconds (the harness's `speed`; the game's default 55).
int& WorldUpdatePeriod() { return *reinterpret_cast<int*>(0x0078FF34); }
constexpr int kDefaultUpdatePeriod = 55;
// The autosave option (nonzero: save "savea" on entering a zone).
bool AutoSaveEnabled() { return *reinterpret_cast<const int*>(0x0079BE7C) != 0; }
const char* AutoSaveName() { return reinterpret_cast<const char*>(0x007942E8); }   // "savea"
// The save game's stream (a memory stream object).
CStream* SaveGameStream() { return reinterpret_cast<CStream*>(0x007AFDF8); }

constexpr std::uint32_t kUnitSelected = 0x10;   // CEngineObject::flags_ of a selected party unit
constexpr int kMessageLogChild = 9;

// The fields of the local player's block (LocalPlayerData) the screen saves.
struct LocalPlayerBlock {
    std::uint8_t unknown_00[0x50];
    int unknown_50;                  // +0x50 saved with the screen (Create reads it back and drops it)
};

CHudTopPanel* TopPanel(CUiWindow* panel) { return static_cast<CHudTopPanel*>(panel); }
CHudMap* Map(CUiWindow* map) { return static_cast<CHudMap*>(map); }

} // namespace

// --- 00635E60: slot 26 --------------------------------------------------------------------------

void CBattleScreenSetup::Close() {
    camera::TheCamera()->flags_ |= CCamera::kFree;
    WorldUpdatePeriod() = kDefaultUpdatePeriod;
    CursorSetHeldOriginal(GameCursor(), nullptr, 0);
    MenuSetOriginal(1);
    MenuCallOriginal();
    PartyArraySetSizeOriginal(&Party(), nullptr, 0, -1);
}
EI_REPLACE_METHOD(0x00635E60, CBattleScreenSetup::Close);

// --- 006360F0: slot 35 --------------------------------------------------------------------------

bool CBattleScreenSetup::PollShown() {
    if (hasHero_) {
        UiDwordArray& party = Party();
        if (party.size_ > 0) CameraSetTargetOriginal(camera::TheCamera(), nullptr, reinterpret_cast<void*>(party.data_[0]));
        if (AutoSaveEnabled()) AutoSaveOriginal(AutoSaveName(), 1);
        hasHero_ = false;
        interface_->ShowHelp(0);
    }
    return true;
}
EI_REPLACE_METHOD(0x006360F0, CBattleScreenSetup::PollShown);

// --- 00636150: slot 28 --------------------------------------------------------------------------

void CBattleScreenSetup::OnItemsPicked(mfc::CTypedPtrList<CItem>* items, int money, int crippled) {
    for (auto* node = items->head; node != nullptr;) {
        auto* next = node->next;
        TopPanelPickedUpOriginal(topPanel_, nullptr, node->data, static_cast<char>(crippled));
        node = next;
    }
    TopPanelMoneyOriginal(topPanel_, nullptr, money);
    TopPanelShowPageOriginal(topPanel_);
    BattleRefreshSelectionOriginal(this);
}
EI_REPLACE_METHOD(0x00636150, CBattleScreenSetup::OnItemsPicked);

// --- 006364E0: slot 30 --------------------------------------------------------------------------

void CBattleScreenSetup::OnUnitLeft(CUnit* unit) {
    if (IsNetworkClientOriginal()) {
        CHudTopPanel* panel = TopPanel(topPanel_);
        reinterpret_cast<mfc::CString*>(&panel->pageTexts_[0])->Empty();
        if (panel->page_ == 0)
            LogSetTextOriginal(panel->owner_->children_.data_[kMessageLogChild], nullptr, game::EmptyText());
        return;
    }
    UiDwordArray& party = Party();
    int index = 0;
    while (index < party.size_ && party.data_[index] != reinterpret_cast<std::uintptr_t>(unit)) ++index;
    if (index >= party.size_) return;
    int after = party.size_ - index - 1;
    if (after != 0) std::memmove(&party.data_[index], &party.data_[index + 1], after * sizeof(party.data_[0]));
    --party.size_;
    PortraitsRebuildOriginal(portraits_, nullptr, 0);
    BattleRefreshSelectionOriginal(this);
}
EI_REPLACE_METHOD(0x006364E0, CBattleScreenSetup::OnUnitLeft);

// --- 00635080: slot 40 --------------------------------------------------------------------------

void CBattleScreenSetup::SaveState() {
    CStream* stream = SaveGameStream();
    UiDwordArray& party = Party();
    // The original collects the ids into a CDWordArray first; the bytes written are the same.
    int count = 0;
    for (int i = 0; i < party.size_; ++i)
        if (reinterpret_cast<CUnit*>(party.data_[i])->flags_ & kUnitSelected) ++count;
    stream->Write(&count, 4);
    for (int i = 0; i < party.size_; ++i) {
        auto* unit = reinterpret_cast<CUnit*>(party.data_[i]);
        if (unit->flags_ & kUnitSelected) stream->Write(&unit->id_, 4);
    }
    stream->Write(&Map(map_)->zoom_, 4);
    stream->Write(&interface_->serverFlag_, 1);
    stream->Write(&reinterpret_cast<const LocalPlayerBlock*>(LocalPlayerData())->unknown_50, 4);
    const char* text = TopPanel(topPanel_)->pageTexts_[0];
    stream->Write(text, static_cast<std::uint32_t>(std::strlen(text) + 1));
    stream->Write(&savedFlag_, 1);
    stream->Write(&savedSelection_, 4);
}
EI_REPLACE_METHOD(0x00635080, CBattleScreenSetup::SaveState);
