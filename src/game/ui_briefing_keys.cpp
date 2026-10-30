// The briefing / base screen's key handler (BriefingScreen::KeyDown). See
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
#include "lib/win_user.h"

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
void OpenObjectives(Interface3D* ui) {
    MapRecord* zone = g_mapDatabase.FindMap(LocalPlayerZone());
    if (!zone) return;
    std::vector<std::string> parts;
    NamedFloatTable quests;
    PlayerZeroVariables(kQuestPrefix, &quests);
    for (auto& node : quests) {
        parts = text::Split(node.first, '.');
        if (parts.size() != kQuestNameParts || std::bit_cast<std::uint32_t>(node.second) != kOneBits) continue;
        MapRecord* questZone = g_mapDatabase.FindMap(parts[1]);
        if (!questZone) continue;
        if (!IsMultiplayerClient() && text::CompareIgnoringCase(questZone->allod, zone->allod) != 0) continue;
        auto* window = new ObjectivesWindow(ui);
        window->Setup(LocalPlayerZone(), nullptr, false, true);
        ui->PushWindow(window, true, true);
        break;
    }
}

} // namespace


void BriefingScreen::KeyDown(int key, int repeat, int modifiers) {
    if (chatActive) {
        if (key == VK_ESCAPE) chatActive = 0;
        return;
    }
    UiContainer::KeyDown(key, repeat, modifiers);
    switch (key) {
    case VK_BACK:
        if (IsMultiplayerClient()) {
            ui->ChatKeyBackspace();
            handled = true;
            return;
        }
        break;
    case VK_RETURN:
        if (IsMultiplayerClient()) {
            ui->ChatKeyEnter();
        } else if (state == 3) {
            ui->PlayInterfaceSound(kClickSound);
            while (state == 3) NextPhrase(true);
        } else if (state == 4) {
            ui->PlayInterfaceSound(kClickSound);
            FinishDialogue();
        }
        handled = true;
        return;
    case VK_ESCAPE:
        if (state == 0) {
            ui->PushWindow(new EscMenu(ui), true, true);
        } else if (state == 3) {
            ui->PlayInterfaceSound(kClickSound);
            while (state == 3) NextPhrase(true);
        } else if (state == 4) {
            ui->PlayInterfaceSound(kClickSound);
            FinishDialogue();
        } else if (state == 2) {
            StartDialogue(true, false);
        } else if (state == 1) {
            state = 0;
        }
        handled = true;
        return;
    case VK_SPACE:
        if (state == 3) {
            ui->PlayInterfaceSound(kClickSound);
            NextPhrase(false);
        } else if (state == 4) {
            ui->PlayInterfaceSound(kClickSound);
            FinishDialogue();
        } else if (state == 2) {
            StartDialogue(true, false);
        }
        handled = true;
        return;
    default:
        break;
    }
    if (state != 0) return;
    switch (g_keyActions[modifiers & kScanCodeMask]) {
    case kActionObjectives:
        OpenObjectives(ui);
        break;
    case kActionLeader:
        if (leader) g_camera.SetTarget(leader);
        handled = true;
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
