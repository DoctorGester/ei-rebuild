// The briefing / base screen's key handler (CBriefingScreen::OnKeyDown, input slot 9). See
// docs/notes/ui.md, "Briefing / base screen".
#include "ui_briefing.h"

#include "camera.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_world.h"

#include <cstring>

namespace {

const char* ClickSound() { return reinterpret_cast<const char*>(0x00793394); }       // "buttons\base\click1.wav"
const char* QuickSaveName() { return reinterpret_cast<const char*>(0x007933AC); }    // "saveq"
const char* QuestPrefix() { return reinterpret_cast<const char*>(0x00783D7C); }      // "q."
CMapDatabase* MapDatabase() { return reinterpret_cast<CMapDatabase*>(0x0079BF00); }

// The escape menu (0x168 bytes, built inline as the battle screen's key handler does).
constexpr std::size_t kEscMenuSize = 0x168;
void** EscMenuVtable() { return reinterpret_cast<void**>(0x007412A4); }
void** EscMenuInputVtable() { return reinterpret_cast<void**>(0x00741270); }
constexpr std::size_t kObjectivesSize = 0x1F0;

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
    return gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}

// Action 4: the objectives window of the current zone, when a quest of a zone in its allod is
// active (inlined in the original).
void OpenObjectives(CInterface3D* ui) {
    MapRecord* zone;
    {
        BriefingPlayerName player;
        zone = MapDatabase()->FindMap(player);
    }
    if (!zone) return;
    console::CStringArray parts;
    BriefingVariables quests(QuestPrefix());
    for (NamedFloatTable::Node* node = quests.First(); node; node = quests.Next(node)) {
        StringSplit(node->key, &parts, '.');
        if (parts.size_ != kQuestNameParts || Bits(node->value) != kOneBits) continue;
        MapRecord* questZone = MapDatabase()->FindMap(parts[1]);
        if (!questZone) continue;
        if (!IsNetworkClientOriginal() && CompareNames(questZone->allod, zone->allod) != 0) continue;
        void* memory = gamecrt::operator_new(kObjectivesSize);
        void* window = memory ? ObjectivesConstructOriginal(memory, nullptr, ui) : nullptr;
        {
            BriefingPlayerName player;
            ObjectivesSetupOriginal(window, nullptr, player, nullptr, 0, 1);
        }
        InterfacePushWindowOriginal(ui, nullptr, window, 1, 1);
        break;
    }
}

} // namespace

// --- 006034A0: input slot 9 ---------------------------------------------------------------------

void CBriefingScreen::OnKeyDown(int key, int repeat, int flags) {
    if (chatActive_) {
        if (key == VK_ESCAPE) chatActive_ = 0;
        return;
    }
    InputOf(this)->BaseKeyDown(key, repeat, flags);
    switch (key) {
    case VK_BACK:
        if (IsNetworkClientOriginal()) {
            ChatKeyBackspaceOriginal(interface_);
            inputHandled_ = true;
            return;
        }
        break;
    case VK_RETURN:
        if (IsNetworkClientOriginal()) {
            ChatKeyEnterOriginal(interface_);
        } else if (state_ == 3) {
            interface_->PlaySound(ClickSound(), 0);
            while (state_ == 3) BriefingNextPhraseOriginal(this, nullptr, 1);
        } else if (state_ == 4) {
            interface_->PlaySound(ClickSound(), 0);
            BriefingTakeRewardsOriginal(this);
        }
        inputHandled_ = true;
        return;
    case VK_ESCAPE:
        if (state_ == 0) {
            void* memory = gamecrt::operator_new(kEscMenuSize);
            CUiWindow* menu = nullptr;
            if (memory) {
                ConstructContainerAt(memory, nullptr, interface_);
                menu = static_cast<CUiWindow*>(memory);
                menu->vtable_ = EscMenuVtable();
                menu->inputVtable_ = EscMenuInputVtable();
            }
            InterfacePushWindowOriginal(interface_, nullptr, menu, 1, 1);
        } else if (state_ == 3) {
            interface_->PlaySound(ClickSound(), 0);
            while (state_ == 3) BriefingNextPhraseOriginal(this, nullptr, 1);
        } else if (state_ == 4) {
            interface_->PlaySound(ClickSound(), 0);
            BriefingTakeRewardsOriginal(this);
        } else if (state_ == 2) {
            BriefingStartDialogueOriginal(this, nullptr, 1, 0);
        } else if (state_ == 1) {
            state_ = 0;
        }
        inputHandled_ = true;
        return;
    case VK_SPACE:
        if (state_ == 3) {
            interface_->PlaySound(ClickSound(), 0);
            BriefingNextPhraseOriginal(this, nullptr, 0);
        } else if (state_ == 4) {
            interface_->PlaySound(ClickSound(), 0);
            BriefingTakeRewardsOriginal(this);
        } else if (state_ == 2) {
            BriefingStartDialogueOriginal(this, nullptr, 1, 0);
        }
        inputHandled_ = true;
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
        if (leader_) CameraSetTargetOriginal(camera::TheCamera(), nullptr, leader_);
        inputHandled_ = true;
        break;
    case kActionQuickSave:
        AutoSaveOriginal(QuickSaveName(), 0);
        break;
    case kActionQuickLoad:
        LoadSavedGameOriginal(QuickSaveName());
        break;
    default:
        break;
    }
}

namespace {
void __fastcall BriefingKeyDown(void* input, void*, int key, int repeat, int flags) {
    WindowFromInput<CBriefingScreen>(input)->OnKeyDown(key, repeat, flags);
}
} // namespace
EI_REPLACE(0x006034A0, BriefingKeyDown);
