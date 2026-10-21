// The camp's input side (buttons, keys), its message-result and help-topic slots (CCampPages,
// ui_camp.h). See docs/notes/ui.md, section 5.
#include "party.h"
#include "ui.h"
#include "ui_camp.h"
#include "game_strings.h"

#include "ui_screens.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "originals_gameplay.h"
#include "originals_ui.h"
#include "originals_world.h"

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
constexpr int kSlotRefreshWeapons = 46;
constexpr std::uint32_t kAnswerYes = 0x10001;

} // namespace

void CCampPages::OnLButtonDown(int buttons, int x, int y) {
    InputOf(this)->OnLeftDown(buttons, x, y);
    int region = RegionAt(x, y);
    switch (region) {
    case kRegionAction:
        static_cast<CCampPages*>(this)->UpdateAction();
        if (actionEnabled_) DoAction();
        break;
    case kRegionSecondAction:
        static_cast<CCampPages*>(this)->UpdateAction();
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
            static_cast<CCampPages*>(this)->ShowPage(region - kRegionFirstPage);
        break;
    }
}

void CCampPages::OnLButtonUp(int buttons, int x, int y) {
    InputOf(this)->OnLeftUp(buttons, x, y);
    ReleaseMouse();
    turning_ = 0;
}

void CCampPages::OnKeyDown(int key, int repeat, int flags) {
    if (key == VK_ESCAPE) {
        static_cast<CCampPages*>(this)->ShowPage(kLeavePage);
        inputHandled_ = true;
        return;
    }
    if ((key == VK_BACK || key == VK_RETURN) && IsMultiplayerClient()) {
        if (key == VK_BACK)
            interface_->ChatKeyBackspace();
        else
            interface_->ChatKeyEnter();
        inputHandled_ = true;
        return;
    }
    InputOf(this)->OnKeyDown(key, repeat, flags);
}

void CCampPages::OnMessageResult(int result) {
    if (static_cast<std::uint32_t>(result) != kAnswerYes) return;
    PartyUnitRecord* hero = ShownHero(this);
    if (static_cast<int>(hero->weaponCount) <= 0) return;
    CItem* weapon = hero->weapons[0];
    CItem* item = LocalPlayerData()->AddItem(weapon, 0, 1);
    hero->RemoveWeapon(weapon);   // 00660A50: the weapon list's Remove
    hero->weaponIndex = 0xFFFFFFFFu;
    static_cast<CUiItemStrip*>(itemStrip_)->ScrollToItem(item);
    CallVirtual(kSlotRefreshWeapons);
}
EI_REPLACE_METHOD(0x0060E900, CCampPages::OnMessageResult);

const char* CCampPages::HelpTopic() { return PageHelpTopic(page_); }
EI_REPLACE_METHOD(0x0060E980, CCampPages::HelpTopic);

namespace {
} // namespace
EI_REPLACE_METHOD(0x0060F470, InputForward<&CCampPages::OnLButtonDown>::Call);
EI_REPLACE_METHOD(0x0060F5B0, InputForward<&CCampPages::OnLButtonUp>::Call);
EI_REPLACE_METHOD(0x0060F600, InputForward<&CCampPages::OnKeyDown>::Call);
