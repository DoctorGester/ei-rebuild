// The objectives window's clicks and its quest places. The window's Build, quest list and
// zone / quest display are in ui_objectives.cpp. See ui.h (ObjectivesWindow) and
// docs/notes/ui.md ("Objectives, save/load, options").
#include "ui.h"
#include "world_glue.h"

#include "console.h"
#include "text_manager.h"
#include "quest_records.h"
#include "ui_screens.h"
#include "world.h"

#include "lib/containers.h"
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

void ObjectivesWindow::LeftDown(int buttons, int x, int y) {
    UiContainer::LeftDown(buttons, x, y);
    int region = RegionAt(x, y);
    std::uint32_t zoneCount = static_cast<std::uint32_t>(zones.end() - zones.begin());
    switch (region) {
    case kRegionOk:
        ui->PlayInterfaceSound(kOkSound);
        if (!twoButtons) {
            ui->SetPendingValue(0);
        } else if (nearestExit >= 0) {
            const ObjectiveExit& exit = exits[nearestExit];
            int index = exit.exit;
            GamePassTime(exit.distance);
            RequestZone(shownZone, index);
        }
        return;
    case kRegionCancel:
        if (twoButtons) {
            ui->PlayInterfaceSound(kCancelSound);
            ui->SetPendingValue(0);
        }
        return;
    case kRegionPreviousZone:
        if (!listZones || zoneCount <= 1) return;
        if (--zoneIndex < 0) zoneIndex = static_cast<int>(zoneCount) - 1;
        ShowZone(zones[zoneIndex]);
        return;
    case kRegionNextZone:
        if (!listZones || zoneCount <= 1) return;
        if (static_cast<std::uint32_t>(++zoneIndex) >= zoneCount) zoneIndex = 0;
        ShowZone(zones[zoneIndex]);
        return;
    default:
        break;
    }
    if (region < kRegionFirstRow) return;
    if (region < kRegionFirstExit) {
        int row = firstRow + region - kRegionFirstRow;
        if (row >= 0 && row < static_cast<int>(quests.size())) {
            ui->PlayInterfaceSound(kSelectSound);
            selectedQuest = row;
            DrawQuestList();
            ShowQuest();
        }
        return;
    }
    if (region >= std::ssize(exits) + kRegionFirstExit) return;
    int exit = region - kRegionFirstExit;
    if (twoButtons && exits[exit].flag) {
        ui->PlayInterfaceSound(kEnterSound);
        SelectExit(exit);
        return;
    }
    ui->PlayInterfaceSound(kCancelSound);
}

void ObjectivesWindow::KeyDown(int key, int repeat, int modifiers) {
    UiContainer::KeyDown(key, repeat, modifiers);
    int step = 0;
    switch (key) {
    case kKeyEnter:
        ui->PlayInterfaceSound(kOkSound);
        if (!twoButtons) {
            ui->SetPendingValue(0);
        } else if (nearestExit >= 0) {
            const ObjectiveExit& exit = exits[nearestExit];
            int index = exit.exit;
            GamePassTime(exit.distance);
            RequestZone(shownZone, index);
        }
        handled = true;
        break;
    case kKeyEsc:
        ui->PlayInterfaceSound(!twoButtons ? kOkSound : kCancelSound);
        ui->SetPendingValue(0);
        handled = true;
        break;
    case kKeyPageUp: step = -5; break;
    case kKeyPageDown: step = 5; break;
    case kKeyUp: step = -1; break;
    case kKeyDown: step = 1; break;
    default: break;
    }
    if (step != 0) {
        ui->PlayInterfaceSound(kSelectSound);
        selectedQuest += step;
        RefreshQuests();
        handled = true;
    }
    if (!handled && g_keyActions[modifiers & 0x1FF] == kActionObjectives && !twoButtons) {
        ui->PlayInterfaceSound(kMessageOkSound);
        ui->SetPendingValue(0);
    }
}

void ObjectivesWindow::LoadQuestPlaces(std::string_view name) {
    std::vector<UiPointF>& places = questPlaces;
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
    TextManager* texts = g_clientWorld.texts;
    texts->Open(kMapText);
    header = str::Format(kQuestHeader, std::string(name));
    if (texts->FindLine(header)) {
        texts->NextLine();
        texts->NextLine();
        words = texts->Split(true);   // none at the end
        if (words.size() > 1) {
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

void ObjectivesWindow::SelectExit(int index) {
    nearestExit = index;
    for (int i = 0; i < std::ssize(exits); ++i) {
        UiMesh* marker = SceneMesh(kFirstExitSprite + i);
        if (i == index) {
            marker->SetScale(markerScale * kNearestScale);
            marker->textureOffsetU = 0.0f;
            blinkTime = 0.0f;
        } else {
            marker->SetScale(markerScale);
            marker->textureOffsetU = exits[i].flag ? 0.0f : kOtherExitU;
        }
    }
    SceneMesh(4)->visible = false;
    SceneMesh(5)->visible = false;
}

void ObjectivesWindow::RefreshQuests() {
    const auto& currentQuests = quests;
    if (static_cast<int>(currentQuests.size()) == 0) return;
    if (selectedQuest < 0) selectedQuest = 0;
    if (selectedQuest >= static_cast<int>(currentQuests.size())) selectedQuest = static_cast<int>(currentQuests.size()) - 1;
    const int row = selectedQuest;
    if (firstRow + kVisibleRows <= row) firstRow = row - (kVisibleRows - 1);
    if (row < firstRow) firstRow = row;
    bar->SetPosition(firstRow);
    firstRow = bar->TakePosition();
    DrawQuestList();
    ShowQuest();
}

void ObjectivesWindow::Update() {
    UiContainer::Update();
    if (bar->TakeMoved()) {
        firstRow = bar->Position();
        DrawQuestList();
    }
    blinkTime += FrameTime();
    const std::uint32_t slow =
        static_cast<std::uint32_t>((std::cos(blinkTime * kSlowRate) * kSlowAmplitude + kSlowBase) * kAlphaScale);
    const std::uint32_t fast =
        static_cast<std::uint32_t>((std::cos(blinkTime * kFastRate) * kFastAmplitude + kFastBase) * kAlphaScale);
    for (int i = lib::Count(exits) + kFirstExitSprite; i < lib::Count(sceneObjects); ++i) {
        UiMesh* place = SceneMesh(i);
        place->flags |= FigureMeshFlag::Flat;
        place->flatColour = fast << 24 | 0xFF0000u;
    }
    for (int i = 0; i < std::ssize(exits); ++i) {
        UiMesh* marker = SceneMesh(kFirstExitSprite + i);
        marker->flags |= FigureMeshFlag::Flat;
        marker->flatColour = i == nearestExit ? fast << 24 | 0xFFFFFFu : slow << 24 | 0xC8C8C8u;
    }
}
