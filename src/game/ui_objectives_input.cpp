// The objectives window's clicks and its quest places. The window's Build, quest list and
// zone / quest display are in ui_objectives.cpp. See ui.h (CObjectivesWindow) and
// docs/notes/ui.md ("Objectives, save/load, options").
#include "ui.h"
#include "world_glue.h"

#include "console.h"
#include "item_db_text.h"
#include "quest_records.h"
#include "ui_screens.h"
#include "world.h"

#include "lib/crt.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr const char* kOkSound = "buttons\\tutorial\\ok.wav";
constexpr const char* kCancelSound = "buttons\\messbox\\cancel.wav";
constexpr const char* kSelectSound = "buttons\\objectives\\select.wav";
constexpr const char* kEnterSound = "buttons\\objectives\\enter.wav";
constexpr const char* kMessageOkSound = "buttons\\messbox\\ok.wav";
constexpr const char* kMapText = "map.txt";
constexpr const char* kQuestHeader = "#quest %s";

constexpr int kRegionOk = 0;
constexpr int kRegionCancel = 1;
constexpr int kRegionPreviousZone = 2;
constexpr int kRegionNextZone = 3;
constexpr int kRegionFirstRow = 4;
constexpr int kRegionFirstExit = 9;

constexpr int kKeyEnter = 0x0D, kKeyEsc = 0x1B, kKeyPageUp = 0x21, kKeyPageDown = 0x22, kKeyUp = 0x26, kKeyDown = 0x28;
constexpr int kActionObjectives = 4;   // "obj" in the key table

constexpr int kFirstExitSprite = 21;     // the exits' markers, then the quest places'
constexpr int kVisibleRows = 5;
constexpr float kNearestScale = 1.2f;
constexpr float kOtherExitU = -0.125f;   // 0xBE000000
// The blink: (cos(rate x t) x amplitude + base) x 255 (2.5, 0.3, 0.5;
// 5.0, 0.4, 0.6; 255).
constexpr float kSlowRate = 2.5f, kSlowAmplitude = 0.3f, kSlowBase = 0.5f;
constexpr float kFastRate = 5.0f, kFastAmplitude = 0.4f, kFastBase = 0.6f;
constexpr float kAlphaScale = 255.0f;

} // namespace

void CObjectivesWindow::LeftDown(int buttons, int x, int y) {
    CUiContainer::LeftDown(buttons, x, y);
    int region = RegionAt(x, y);
    std::uint32_t zoneCount = static_cast<std::uint32_t>(zones_.end() - zones_.begin());
    switch (region) {
    case kRegionOk:
        interface_->PlaySound(kOkSound);
        if (!twoButtons_) {
            interface_->SetPendingValue(0);
        } else if (nearestExit_ >= 0) {
            const ObjectiveExit& exit = exits_[nearestExit_];
            int index = exit.exit_;
            GamePassTime(exit.distance_);
            RequestZone(shownZone_, index);
        }
        return;
    case kRegionCancel:
        if (twoButtons_) {
            interface_->PlaySound(kCancelSound);
            interface_->SetPendingValue(0);
        }
        return;
    case kRegionPreviousZone:
        if (!listZones_ || zoneCount <= 1) return;
        if (--zoneIndex_ < 0) zoneIndex_ = static_cast<int>(zoneCount) - 1;
        ShowZone(zones_[zoneIndex_]);
        return;
    case kRegionNextZone:
        if (!listZones_ || zoneCount <= 1) return;
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
            interface_->PlaySound(kSelectSound);
            selectedQuest_ = row;
            DrawQuestList();
            ShowQuest();
        }
        return;
    }
    if (region >= std::ssize(exits_) + kRegionFirstExit) return;
    int exit = region - kRegionFirstExit;
    if (twoButtons_ && exits_[exit].flag_) {
        interface_->PlaySound(kEnterSound);
        SelectExit(exit);
        return;
    }
    interface_->PlaySound(kCancelSound);
}

void CObjectivesWindow::KeyDown(int key, int repeat, int flags) {
    CUiContainer::KeyDown(key, repeat, flags);
    int step = 0;
    switch (key) {
    case kKeyEnter:
        interface_->PlaySound(kOkSound);
        if (!twoButtons_) {
            interface_->SetPendingValue(0);
        } else if (nearestExit_ >= 0) {
            const ObjectiveExit& exit = exits_[nearestExit_];
            int index = exit.exit_;
            GamePassTime(exit.distance_);
            RequestZone(shownZone_, index);
        }
        handled_ = true;
        break;
    case kKeyEsc:
        interface_->PlaySound(!twoButtons_ ? kOkSound : kCancelSound);
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
        interface_->PlaySound(kSelectSound);
        selectedQuest_ += step;
        RefreshQuests();
        handled_ = true;
    }
    if (!handled_ && g_keyActions[flags & 0x1FF] == kActionObjectives && !twoButtons_) {
        interface_->PlaySound(kMessageOkSound);
        interface_->SetPendingValue(0);
    }
}

void CObjectivesWindow::LoadQuestPlaces(std::string_view name) {
    std::vector<UiPointF>& places = questPlaces_;
    places.clear();
    if (IsMultiplayerClient()) {
        // (The index's operator[] adds a null entry for a new name.)
        QuestRecord* record = g_questDatabase.questsByName[std::string(name)];
        if (record == nullptr) return;
        for (std::uint32_t i = 1; i < record->places.size(); i += 2)
            places.push_back(UiPointF{record->places[i - 1], record->places[i]});
        return;
    }
    std::string header;
    std::vector<std::string> words;
    CTextManager* texts = g_clientWorld->texts_;
    texts->Open(kMapText);
    header = str::Format(kQuestHeader, std::string(name));
    if (texts->FindLine(header)) {
        texts->NextLine();
        texts->NextLine();
        if (texts->Split(&words, 1) && words.size() > 1) {
            for (std::size_t i = 1; i < words.size(); i += 2) {
                UiPointF place;
                place.x = static_cast<float>(str::ToDouble(words[i - 1]));
                place.y = static_cast<float>(str::ToDouble(words[i]));
                places.push_back(place);
            }
        }
    }
    texts->Close();
}

// --- Quest list handlers --------------------------------------------------------

void CObjectivesWindow::SelectExit(int index) {
    nearestExit_ = index;
    for (int i = 0; i < std::ssize(exits_); ++i) {
        CUiMesh* marker = SceneMesh(kFirstExitSprite + i);
        if (i == index) {
            marker->SetScale(markerScale_ * kNearestScale);
            marker->textureOffsetU_ = 0.0f;
            blinkTime_ = 0.0f;
        } else {
            marker->SetScale(markerScale_);
            marker->textureOffsetU_ = exits_[i].flag_ ? 0.0f : kOtherExitU;
        }
    }
    SceneMesh(4)->visible_ = false;
    SceneMesh(5)->visible_ = false;
}

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

void CObjectivesWindow::Update() {
    CUiContainer::Update();
    int first;
    if (bar_->TakePosition(&first)) {
        firstRow_ = first;
        DrawQuestList();
    }
    // The sum stays unrounded for the alphas; the stored time is rounded to a float.
    const double time = FrameTime() + blinkTime_;
    blinkTime_ = static_cast<float>(time);
    const std::uint32_t slow = libcrt::Ftol((x87::fcos(time * kSlowRate) * kSlowAmplitude + kSlowBase) * kAlphaScale);
    const std::uint32_t fast = libcrt::Ftol((x87::fcos(time * kFastRate) * kFastAmplitude + kFastBase) * kAlphaScale);
    for (int i = std::ssize(exits_) + kFirstExitSprite; i < std::ssize(sceneObjects_); ++i) {
        CUiMesh* place = SceneMesh(i);
        place->flags_ |= FigureMeshFlag::Flat;
        place->flatColour_ = fast << 24 | 0xFF0000u;
    }
    for (int i = 0; i < std::ssize(exits_); ++i) {
        CUiMesh* marker = SceneMesh(kFirstExitSprite + i);
        marker->flags_ |= FigureMeshFlag::Flat;
        marker->flatColour_ = i == nearestExit_ ? fast << 24 | 0xFFFFFFu : slow << 24 | 0xC8C8C8u;
    }
}
