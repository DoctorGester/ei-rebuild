// The objectives window's clicks and its quest places. The window's Build, quest list and
// zone / quest display are in ui_objectives.cpp. See ui.h (CObjectivesWindow) and
// docs/notes/ui.md ("Objectives, save/load, options").
#include "ui.h"
#include "world_glue.h"
#include "game_strings.h"

#include "console.h"
#include "item_db_text.h"
#include "quest_records.h"
#include "text_reader.h"
#include "ui_screens.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cmath>
#include <cstdlib>
#include <string>

namespace {

const char* OkSound() { return "buttons\\tutorial\\ok.wav"; }
const char* CancelSound() { return "buttons\\messbox\\cancel.wav"; }
const char* SelectSound() { return "buttons\\objectives\\select.wav"; }
const char* EnterSound() { return "buttons\\objectives\\enter.wav"; }
const char* MessageOkSound() { return "buttons\\messbox\\ok.wav"; }
const char* MapText() { return "map.txt"; }
const char* QuestHeader() { return "#quest %s"; }

constexpr int kRegionOk = 0;
constexpr int kRegionCancel = 1;
constexpr int kRegionPreviousZone = 2;
constexpr int kRegionNextZone = 3;
constexpr int kRegionFirstRow = 4;
constexpr int kRegionFirstExit = 9;

// The quest database's index (operator[] adds an empty slot for an unknown name).
QuestDatabase::QuestIndex& QuestIndex() { return TheQuestDatabase().questsByName; }

} // namespace

void CObjectivesWindow::OnLButtonDown(int buttons, int x, int y) {
    CUiContainer::LeftDown(buttons, x, y);
    int region = RegionAt(x, y);
    std::uint32_t zoneCount = static_cast<std::uint32_t>(zones_.end() - zones_.begin());
    switch (region) {
    case kRegionOk:
        interface_->PlaySound(OkSound(), 0);
        if (twoButtons_ == 0) {
            interface_->SetPendingValue(0);
        } else if (nearestExit_ >= 0) {
            const ObjectiveExit& exit = exits_[nearestExit_];
            int index = exit.exit_;
            GamePassTime(exit.distance_);
            RequestZone(shownZone_.c_str(), index);
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
        ShowZone(zones_[zoneIndex_]);
        return;
    case kRegionNextZone:
        if (listZones_ == 0 || zoneCount <= 1) return;
        if (static_cast<std::uint32_t>(++zoneIndex_) >= zoneCount) zoneIndex_ = 0;
        ShowZone(zones_[zoneIndex_]);
        return;
    default:
        break;
    }
    if (region < kRegionFirstRow) return;
    if (region < kRegionFirstExit) {
        int row = firstRow_ + region - kRegionFirstRow;
        if (row >= 0 && row < static_cast<int>(quests_.size())) {
            interface_->PlaySound(SelectSound(), 0);
            selectedQuest_ = row;
            DrawQuestList();
            ShowQuest();
        }
        return;
    }
    if (region >= exits_.size + kRegionFirstExit) return;
    int exit = region - kRegionFirstExit;
    if (twoButtons_ != 0 && exits_[exit].flag_) {
        interface_->PlaySound(EnterSound(), 0);
        SelectExit(exit);
        return;
    }
    interface_->PlaySound(CancelSound(), 0);
}

namespace {
constexpr int kKeyEnter = 0x0D, kKeyEsc = 0x1B, kKeyPageUp = 0x21, kKeyPageDown = 0x22, kKeyUp = 0x26, kKeyDown = 0x28;
constexpr int kActionObjectives = 4;   // "obj" in the key table
} // namespace

void CObjectivesWindow::OnKeyDown(int key, int repeat, int flags) {
    CUiContainer::KeyDown(key, repeat, flags);
    int step = 0;
    switch (key) {
    case kKeyEnter:
        interface_->PlaySound(OkSound(), 0);
        if (twoButtons_ == 0) {
            interface_->SetPendingValue(0);
        } else if (nearestExit_ >= 0) {
            const ObjectiveExit& exit = exits_[nearestExit_];
            int index = exit.exit_;
            GamePassTime(exit.distance_);
            RequestZone(shownZone_.c_str(), index);
        }
        handled_ = true;
        break;
    case kKeyEsc:
        interface_->PlaySound(twoButtons_ == 0 ? OkSound() : CancelSound(), 0);
        interface_->SetPendingValue(0);
        handled_ = true;
        break;
    case kKeyPageUp: step = -5; break;
    case kKeyPageDown: step = 5; break;
    case kKeyUp: step = -1; break;
    case kKeyDown: step = 1; break;
    default: break;
    }
    if (step != 0) {
        interface_->PlaySound(SelectSound(), 0);
        selectedQuest_ += step;
        RefreshQuests();
        handled_ = true;
    }
    if (!handled_ && KeyActions()[flags & 0x1FF] == kActionObjectives && twoButtons_ == 0) {
        interface_->PlaySound(MessageOkSound(), 0);
        interface_->SetPendingValue(0);
    }
}

EI_REWRITTEN(0x0064E330, CObjectivesWindow::OnLButtonDown);
EI_REWRITTEN(0x0064E5A0, CObjectivesWindow::OnKeyDown);

void CObjectivesWindow::LoadQuestPlaces(const char* name) {
    stlp::vector<UiPointF>& places = questPlaces_;
    places.clear();
    if (IsMultiplayerClient()) {
        std::string key(name);
        QuestRecord* record = QuestIndex().Subscript(key);
        if (record == nullptr) return;
        for (std::uint32_t i = 1; i < record->places.size(); i += 2)
            places.push_back(UiPointF{record->places[i - 1], record->places[i]});
        return;
    }
    std::string header;
    console::CStringArray words;
    CTextManager* texts = TheTextManager();
    texts->Open(MapText());
    header = str::Format(QuestHeader(), name);
    if (texts->FindLine(header.c_str())) {
        TextNextLine(texts);
        TextNextLine(texts);
        if (texts->Split(&words, 1) && words.GetSize() > 1) {
            for (int i = 1; i < words.GetSize(); i += 2) {
                UiPointF place;
                place.x = static_cast<float>(std::atof(words[i - 1]));
                place.y = static_cast<float>(std::atof(words[i]));
                places.push_back(place);
            }
        }
    }
    TextClose(texts);
}
EI_REWRITTEN(0x0064EA80, CObjectivesWindow::LoadQuestPlaces);

// --- Quest list handlers --------------------------------------------------------

namespace {
constexpr int kFirstExitSprite = 21;     // the exits' markers, then the quest places'
constexpr int kVisibleRows = 5;
constexpr float kNearestScale = 1.2f;
constexpr float kOtherExitU = -0.125f;   // 0xBE000000
constexpr std::uint32_t kPlainColour = 0x100;   // CUiMesh::flags_: drawn in flatColour_
// The blink: (cos(rate x t) x amplitude + base) x 255 (2.5, 0.3, 0.5;
// 5.0, 0.4, 0.6; 255).
constexpr float kSlowRate = 2.5f, kSlowAmplitude = 0.3f, kSlowBase = 0.5f;
constexpr float kFastRate = 5.0f, kFastAmplitude = 0.4f, kFastBase = 0.6f;
constexpr float kAlphaScale = 255.0f;

CUiMesh* Sprite(const CObjectivesWindow* window, int index) {
    return window->SceneMesh(index);
}
} // namespace

void CObjectivesWindow::SelectExit(int index) {
    nearestExit_ = index;
    for (int i = 0; i < exits_.size; ++i) {
        CUiMesh* marker = Sprite(this, kFirstExitSprite + i);
        if (i == index) {
            marker->SetScale(markerScale_ * kNearestScale);
            marker->textureOffsetU_ = 0.0f;
            blinkTime_ = 0.0f;
        } else {
            marker->SetScale(markerScale_);
            marker->textureOffsetU_ = exits_[i].flag_ ? 0.0f : kOtherExitU;
        }
    }
    Sprite(this, 4)->visible_ = 0;
    Sprite(this, 5)->visible_ = 0;
}
EI_REWRITTEN(0x0064E990, CObjectivesWindow::SelectExit);

void CObjectivesWindow::RefreshQuests() {
    const auto& quests = quests_;
    if (static_cast<int>(quests.size()) == 0) return;
    if (selectedQuest_ < 0) selectedQuest_ = 0;
    if (selectedQuest_ >= static_cast<int>(quests.size())) selectedQuest_ = static_cast<int>(quests.size()) - 1;
    const int row = selectedQuest_;
    if (firstRow_ + kVisibleRows <= row) firstRow_ = row - (kVisibleRows - 1);
    if (row < firstRow_) firstRow_ = row;
    bar_->SetPosition(firstRow_);
    bar_->TakePosition(&firstRow_);
    DrawQuestList();
    ShowQuest();
}
EI_REWRITTEN(0x0064E8B0, CObjectivesWindow::RefreshQuests);

void CObjectivesWindow::Update() {
    UpdateBase();
    int first;
    if (bar_->TakePosition(&first)) {
        firstRow_ = first;
        DrawQuestList();
    }
    // The sum stays unrounded for the alphas; the stored time is rounded to a float.
    const double time = FrameTime() + blinkTime_;
    blinkTime_ = static_cast<float>(time);
    const std::uint32_t slow = gamecrt::Ftol((x87::fcos(time * kSlowRate) * kSlowAmplitude + kSlowBase) * kAlphaScale);
    const std::uint32_t fast = gamecrt::Ftol((x87::fcos(time * kFastRate) * kFastAmplitude + kFastBase) * kAlphaScale);
    for (int i = exits_.size + kFirstExitSprite; i < sceneObjects_.size; ++i) {
        CUiMesh* place = Sprite(this, i);
        place->flags_ |= kPlainColour;
        place->flatColour_ = fast << 24 | 0xFF0000u;
    }
    for (int i = 0; i < exits_.size; ++i) {
        CUiMesh* marker = Sprite(this, kFirstExitSprite + i);
        marker->flags_ |= kPlainColour;
        marker->flatColour_ = i == nearestExit_ ? fast << 24 | 0xFFFFFFu : slow << 24 | 0xC8C8C8u;
    }
}
EI_REWRITTEN(0x0064E760, CObjectivesWindow::Update);
