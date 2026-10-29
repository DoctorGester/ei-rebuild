// The briefing / base screen's key handler (CBriefingScreen::KeyDown). See
// docs/notes/ui.md, "Briefing / base screen".
#include "camera.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "text_split.h"
#include "ui.h"
#include "ui_briefing.h"
#include "world.h"
#include "world_glue.h"

#include "lib/text_case.h"

#include <bit>
#include <cstring>

namespace {

constexpr const char* kClickSound = "buttons\\base\\click1.wav";
constexpr const char* kQuickSaveName = "saveq";
constexpr const char* kQuestPrefix = "q.";


constexpr int kScanCodeMask = 0x1FF;
constexpr int kActionObjectives = 4;
constexpr int kActionLeader = 0xB;
constexpr int kActionQuickSave = '5';
constexpr int kActionQuickLoad = '6';
constexpr std::uint32_t kOneBits = 0x3F800000;   // 1.0f
constexpr int kQuestNameParts = 3;               // "q", zone, quest

// Action 4: the objectives window of the current zone, when a quest of a zone in its allod is
// active.
void OpenObjectives(CInterface3D* ui) {
    MapRecord* zone = g_mapDatabase.FindMap(LocalPlayerZone());
    if (!zone) return;
    std::vector<std::string> parts;
    NamedFloatTable quests;
    PlayerZeroVariables(kQuestPrefix, &quests);
    for (auto& node : quests) {
        text::Split(node.first, &parts, '.');
        if (parts.size() != kQuestNameParts || std::bit_cast<std::uint32_t>(node.second) != kOneBits) continue;
        MapRecord* questZone = g_mapDatabase.FindMap(parts[1]);
        if (!questZone) continue;
        if (!IsMultiplayerClient() && text::CompareIgnoringCase(questZone->allod, zone->allod) != 0) continue;
        auto* window = new CObjectivesWindow(ui);
        window->Setup(LocalPlayerZone(), nullptr, false, true);
        ui->PushWindow(window, 1, 1);
        break;
    }
}

} // namespace


void CBriefingScreen::KeyDown(int key, int repeat, int flags) {
    if (chatActive_) {
        if (key == VK_ESCAPE) chatActive_ = 0;
        return;
    }
    CUiContainer::KeyDown(key, repeat, flags);
    switch (key) {
    case VK_BACK:
        if (IsMultiplayerClient()) {
            interface_->ChatKeyBackspace();
            handled_ = true;
            return;
        }
        break;
    case VK_RETURN:
        if (IsMultiplayerClient()) {
            interface_->ChatKeyEnter();
        } else if (state_ == 3) {
            interface_->PlaySound(kClickSound);
            while (state_ == 3) NextPhrase(true);
        } else if (state_ == 4) {
            interface_->PlaySound(kClickSound);
            FinishDialogue();
        }
        handled_ = true;
        return;
    case VK_ESCAPE:
        if (state_ == 0) {
            interface_->PushWindow(new CEscMenu(interface_), 1, 1);
        } else if (state_ == 3) {
            interface_->PlaySound(kClickSound);
            while (state_ == 3) NextPhrase(true);
        } else if (state_ == 4) {
            interface_->PlaySound(kClickSound);
            FinishDialogue();
        } else if (state_ == 2) {
            StartDialogue(true, false);
        } else if (state_ == 1) {
            state_ = 0;
        }
        handled_ = true;
        return;
    case VK_SPACE:
        if (state_ == 3) {
            interface_->PlaySound(kClickSound);
            NextPhrase(false);
        } else if (state_ == 4) {
            interface_->PlaySound(kClickSound);
            FinishDialogue();
        } else if (state_ == 2) {
            StartDialogue(true, false);
        }
        handled_ = true;
        return;
    default:
        break;
    }
    if (state_ != 0) return;
    switch (g_keyActions[flags & kScanCodeMask]) {
    case kActionObjectives:
        OpenObjectives(interface_);
        break;
    case kActionLeader:
        if (leader_) g_camera->SetTarget(leader_);
        handled_ = true;
        break;
    case kActionQuickSave:
        ::QuickSave(kQuickSaveName, false);
        break;
    case kActionQuickLoad:
        QuickLoad(kQuickSaveName);
        break;
    default:
        break;
    }
}
