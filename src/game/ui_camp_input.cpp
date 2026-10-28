// The camp's input side (buttons, keys), its message-result and help-topic slots (CCampPages,
// ui_camp.h). See docs/notes/ui.md, section 5.
#include "party.h"
#include "ui.h"
#include "ui_camp.h"
#include "game_strings.h"

#include "ui_screens.h"
#include "unit.h"
#include "world.h"

namespace {

const char* MoveSound() { return "buttons\\camp\\move.wav"; }

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

void CCampPages::OnLButtonDown(int buttons, int x, int y) {
    CUiContainer::LeftDown(buttons, x, y);
    int region = RegionAt(x, y);
    switch (region) {
    case kRegionAction:
        UpdateAction();
        if (actionEnabled_) DoAction();
        break;
    case kRegionSecondAction:
        UpdateAction();
        if (actionShown_) UndoAction();
        break;
    case kRegionTurnLeft:
    case kRegionTurnRight:
        if (page_ == kEquipmentPage) {
            interface_->PlaySound(MoveSound(), 0);
            turning_ = region == kRegionTurnLeft ? 1 : 2;
            CaptureMouse();
        }
        break;
    default:
        if (region >= kRegionFirstPage && region <= kRegionLastPage && page_ != region - kRegionFirstPage)
            ShowPage(region - kRegionFirstPage);
        break;
    }
}

void CCampPages::OnLButtonUp(int buttons, int x, int y) {
    CUiContainer::LeftUp(buttons, x, y);
    ReleaseMouse();
    turning_ = 0;
}

void CCampPages::OnKeyDown(int key, int repeat, int flags) {
    if (key == VK_ESCAPE) {
        ShowPage(kLeavePage);
        handled_ = true;
        return;
    }
    if ((key == VK_BACK || key == VK_RETURN) && IsMultiplayerClient()) {
        if (key == VK_BACK)
            interface_->ChatKeyBackspace();
        else
            interface_->ChatKeyEnter();
        handled_ = true;
        return;
    }
    CUiContainer::KeyDown(key, repeat, flags);
}

void CCampPages::OnMessageResult(int result) {
    if (static_cast<std::uint32_t>(result) != kAnswerYes) return;
    PartyUnitRecord* hero = HeroRecord();
    if (hero->weapons.empty()) return;
    CItem* weapon = hero->weapons[0];
    CItem* item = LocalPlayerData()->AddItem(weapon, 0, 1);
    hero->RemoveWeapon(weapon);   // the weapon list's Remove
    hero->weaponIndex = 0xFFFFFFFFu;
    itemStrip_->ScrollToItem(item);
    RefreshWeapons();
}

const char* CCampPages::HelpTopic() { return PageHelpTopic(page_); }
