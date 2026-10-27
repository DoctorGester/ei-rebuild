// The briefing / base screen's key handler (CBriefingScreen::OnKeyDown, input slot 9). See
// docs/notes/ui.md, "Briefing / base screen".
#include "game_strings.h"
#include "ui.h"
#include "ui_briefing.h"

#include "camera.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "text_split.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstring>

namespace {

const char* ClickSound() { return "buttons\\base\\click1.wav"; }
const char* QuickSaveName() { return "saveq"; }
const char* QuestPrefix() { return "q."; }


constexpr int kScanCodeMask = 0x1FF;
constexpr int kActionObjectives = 4;
constexpr int kActionLeader = 0xB;
constexpr int kActionQuickSave = '5';
constexpr int kActionQuickLoad = '6';
constexpr std::uint32_t kOneBits = 0x3F800000;   // 1.0f
constexpr int kQuestNameParts = 3;               // "q", zone, quest

std::uint32_t Bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

int CompareNames(const char* a, const char* b) {
    return gamecrt::mbsicmp(a, b);
}

// Action 4: the objectives window of the current zone, when a quest of a zone in its allod is
// active.
void OpenObjectives(CInterface3D* ui) {
    MapRecord* zone;
    {
        BriefingPlayerName player;
        zone = TheMapDatabase()->FindMap(player);
    }
    if (!zone) return;
    console::CStringArray parts;
    BriefingVariables quests(QuestPrefix());
    for (NamedFloatTable::Node* node = quests.First(); node; node = quests.Next(node)) {
        text::Split(node->value.key, &parts, '.');
        if (parts.size != kQuestNameParts || Bits(node->value.mapped) != kOneBits) continue;
        MapRecord* questZone = TheMapDatabase()->FindMap(parts[1]);
        if (!questZone) continue;
        if (!IsMultiplayerClient() && CompareNames(questZone->allod.c_str(), zone->allod.c_str()) != 0) continue;
        auto* window = new CObjectivesWindow(ui);
        {
            BriefingPlayerName player;
            window->Setup(player, nullptr, 0, 1);
        }
        ui->PushWindow(window, 1, 1);
        break;
    }
}

} // namespace


void CBriefingScreen::OnKeyDown(int key, int repeat, int flags) {
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
            interface_->PlaySound(ClickSound(), 0);
            while (state_ == 3) NextPhrase(1);
        } else if (state_ == 4) {
            interface_->PlaySound(ClickSound(), 0);
            FinishDialogue();
        }
        handled_ = true;
        return;
    case VK_ESCAPE:
        if (state_ == 0) {
            interface_->PushWindow(new CEscMenu(interface_), 1, 1);
        } else if (state_ == 3) {
            interface_->PlaySound(ClickSound(), 0);
            while (state_ == 3) NextPhrase(1);
        } else if (state_ == 4) {
            interface_->PlaySound(ClickSound(), 0);
            FinishDialogue();
        } else if (state_ == 2) {
            StartDialogue(1, 0);
        } else if (state_ == 1) {
            state_ = 0;
        }
        handled_ = true;
        return;
    case VK_SPACE:
        if (state_ == 3) {
            interface_->PlaySound(ClickSound(), 0);
            NextPhrase(0);
        } else if (state_ == 4) {
            interface_->PlaySound(ClickSound(), 0);
            FinishDialogue();
        } else if (state_ == 2) {
            StartDialogue(1, 0);
        }
        handled_ = true;
        return;
    default:
        break;
    }
    if (state_ != 0) return;
    switch (KeyActions()[flags & kScanCodeMask]) {
    case kActionObjectives:
        OpenObjectives(interface_);
        break;
    case kActionLeader:
        if (leader_) TheCamera()->SetTarget(leader_);
        handled_ = true;
        break;
    case kActionQuickSave:
        ::QuickSave(QuickSaveName(), static_cast<char>(0));
        break;
    case kActionQuickLoad:
        QuickLoad(QuickSaveName());
        break;
    default:
        break;
    }
}

EI_REWRITTEN(0x006034A0, CBriefingScreen::OnKeyDown);
