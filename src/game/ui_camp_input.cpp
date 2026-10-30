// The camp's input side (buttons, keys), its message-result and help-topic slots (CampPages,
// ui_camp.h). See docs/notes/ui.md, section 5.
#include "party.h"
#include "ui.h"
#include "ui_camp.h"

#include "ui_screens.h"
#include "unit.h"
#include "world.h"

#include <windows.h>

namespace {

constexpr const char* kMoveSound = "buttons\\camp\\move.wav";

// The help topics of pages 0-6 (the game's strings).
const char* PageHelpTopic(int page) {
    switch (page) {
    case 0: return "camp_weapons";
    case 1: return "camp_skills";
    case 2: return "camp_spell_trade";
    case 3: return "camp_spell_constr";
    case 4: return "camp_item_trade";
    case 5: return "camp_item_constr";
    case 6: return "camp_repair";
    default: return nullptr;
    }
}

constexpr int kRegionAction = 0;
constexpr int kRegionSecondAction = 1;
constexpr int kRegionTurnLeft = 2;
constexpr int kRegionTurnRight = 3;
constexpr int kRegionFirstPage = 4;
constexpr int kRegionLastPage = 11;
constexpr int kEquipmentPage = 0;
constexpr int kLeavePage = 7;
constexpr std::uint32_t kAnswerYes = 0x10001;

} // namespace

void CampPages::LeftDown(int buttons, int x, int y) {
    UiContainer::LeftDown(buttons, x, y);
    int region = RegionAt(x, y);
    switch (region) {
    case kRegionAction:
        UpdateAction();
        if (actionEnabled) DoAction();
        break;
    case kRegionSecondAction:
        UpdateAction();
        if (actionShown) UndoAction();
        break;
    case kRegionTurnLeft:
    case kRegionTurnRight:
        if (page == kEquipmentPage) {
            ui->PlayInterfaceSound(kMoveSound);
            turning = region == kRegionTurnLeft ? 1 : 2;
            CaptureMouse();
        }
        break;
    default:
        if (region >= kRegionFirstPage && region <= kRegionLastPage && page != region - kRegionFirstPage)
            ShowPage(region - kRegionFirstPage);
        break;
    }
}

void CampPages::LeftUp(int buttons, int x, int y) {
    UiContainer::LeftUp(buttons, x, y);
    ReleaseMouse();
    turning = 0;
}

void CampPages::KeyDown(int key, int repeat, int modifiers) {
    if (key == VK_ESCAPE) {
        ShowPage(kLeavePage);
        handled = true;
        return;
    }
    if ((key == VK_BACK || key == VK_RETURN) && IsMultiplayerClient()) {
        if (key == VK_BACK)
            ui->ChatKeyBackspace();
        else
            ui->ChatKeyEnter();
        handled = true;
        return;
    }
    UiContainer::KeyDown(key, repeat, modifiers);
}

void CampPages::MessageResult(int result) {
    if (static_cast<std::uint32_t>(result) != kAnswerYes) return;
    PartyUnitRecord* heroRecord = HeroRecord();
    if (heroRecord->weapons.empty()) return;
    Item* weapon = heroRecord->weapons[0];
    Item* item = LocalPlayerData()->AddItem(weapon, 0, 1);
    heroRecord->RemoveWeapon(weapon);   // the weapon list's Remove
    heroRecord->weaponIndex = kNoWeaponSlot;
    itemStrip->ScrollToItem(item);
    RefreshWeapons();
}

const char* CampPages::HelpTopic() { return PageHelpTopic(page); }
