// The objectives window's clicks and its quest places (A-H17). The window's Build, quest list and
// zone / quest display are in ui_objectives.cpp. See ui.h (CObjectivesWindow) and
// docs/notes/items_spells.md (A-H17).
#include "ui.h"

#include "console.h"
#include "item_db_text.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "quest_records.h"
#include "text_reader.h"
#include "ui_screens.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/stlp.h"

namespace {

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
const char* OkSound() { return Text(0x0079477C); }       // "buttons\tutorial\ok.wav"
const char* CancelSound() { return Text(0x007937E4); }   // "buttons\messbox\cancel.wav"
const char* SelectSound() { return Text(0x0079475C); }   // "buttons\objectives\select.wav"
const char* EnterSound() { return Text(0x0079473C); }    // "buttons\objectives\enter.wav"
const char* MapText() { return Text(0x007856A0); }       // "map.txt"
const char* QuestHeader() { return Text(0x007941D4); }   // "#quest %s"

constexpr int kRegionOk = 0;
constexpr int kRegionCancel = 1;
constexpr int kRegionPreviousZone = 2;
constexpr int kRegionNextZone = 3;
constexpr int kRegionFirstRow = 4;
constexpr int kRegionFirstExit = 9;

// 00578700 (the client glue 007AFEA6, not read; RET 4): with a distance of at least 0, the
// players' times move on by it (0058D280).
__declspec(naked) void __fastcall PassTravelTimeOriginal(void*, void*, float /*distance*/) EI_ORIGINAL(0x00578700)
void* ClientGlue() { return reinterpret_cast<void*>(0x007AFEA6); }

// The quest database's index (007C87B4; operator[] adds an empty slot for an unknown name).
void* QuestIndex() { return reinterpret_cast<void*>(0x007C87B4); }

} // namespace

void CObjectivesWindow::OnLButtonDown(int buttons, int x, int y) {
    InputOf(this)->BaseLButtonDown(buttons, x, y);
    int region = RegionAt(x, y);
    std::uint32_t zoneCount = static_cast<std::uint32_t>(zonesEnd_ - zonesBegin_);
    switch (region) {
    case kRegionOk:
        interface_->PlaySound(OkSound(), 0);
        if (twoButtons_ == 0) {
            interface_->SetPendingValue(0);
        } else if (nearestExit_ >= 0) {
            const ObjectiveExit& exit = exits_[nearestExit_];
            int index = exit.exit_;
            PassTravelTimeOriginal(ClientGlue(), nullptr, exit.distance_);
            RequestZone(shownZone_, index);
        }
        return;
    case kRegionCancel:
        if (twoButtons_ != 0) {
            interface_->PlaySound(CancelSound(), 0);
            interface_->SetPendingValue(0);
        }
        return;
    case kRegionPreviousZone:
        if (listZones_ == 0 || zoneCount <= 1) return;
        if (--zoneIndex_ < 0) zoneIndex_ = static_cast<int>(zoneCount) - 1;
        ShowZone(zonesBegin_[zoneIndex_]);
        return;
    case kRegionNextZone:
        if (listZones_ == 0 || zoneCount <= 1) return;
        if (static_cast<std::uint32_t>(++zoneIndex_) >= zoneCount) zoneIndex_ = 0;
        ShowZone(zonesBegin_[zoneIndex_]);
        return;
    default:
        break;
    }
    if (region < kRegionFirstRow) return;
    if (region < kRegionFirstExit) {
        int row = unknown_1C0 + region - kRegionFirstRow;
        if (row >= 0 && row < reinterpret_cast<mfc::CStringArray*>(quests_)->GetSize()) {
            interface_->PlaySound(SelectSound(), 0);
            selectedQuest_ = row;
            DrawQuestList();
            ShowQuest();
        }
        return;
    }
    if (region >= exitCount_ + kRegionFirstExit) return;
    int exit = region - kRegionFirstExit;
    if (twoButtons_ != 0 && exits_[exit].flag_) {
        interface_->PlaySound(EnterSound(), 0);
        SelectExit(exit);
        return;
    }
    interface_->PlaySound(CancelSound(), 0);
}

namespace {
void __fastcall ObjectivesLButtonDown(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CObjectivesWindow>(input)->OnLButtonDown(buttons, x, y);
}
} // namespace
EI_REPLACE(0x0064E330, ObjectivesLButtonDown);

void CObjectivesWindow::LoadQuestPlaces(const char* name) {
    stlp::vector<UiPointF>& places = QuestPlaces();
    places.clear();
    if (IsMultiplayerClient()) {
        stlp::string key(name);
        auto* record = static_cast<QuestRecord*>(*QuestIndexAt(QuestIndex(), nullptr, &key));
        if (record == nullptr) return;
        for (std::uint32_t i = 1; i < record->places.size(); i += 2)
            places.push_back(UiPointF{record->places[i - 1], record->places[i]});
        return;
    }
    mfc::CString header;
    console::CStringArray words;
    CTextManager* texts = GameTexts();
    TextOpen(texts, nullptr, MapText());
    header.Format(QuestHeader(), name);
    if (TextFindLine(texts, nullptr, header)) {
        TextNextLine(texts);
        TextNextLine(texts);
        if (TextSplit(texts, nullptr, &words, 1) && words.GetSize() > 1) {
            for (int i = 1; i < words.GetSize(); i += 2) {
                UiPointF place;
                place.x = static_cast<float>(GameAtof(words[i - 1]));
                place.y = static_cast<float>(GameAtof(words[i]));
                places.push_back(place);
            }
        }
    }
    TextClose(texts);
}
EI_REPLACE_METHOD(0x0064EA80, CObjectivesWindow::LoadQuestPlaces);
