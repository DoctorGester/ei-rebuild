// The camp's input side (buttons, keys), its message-result and help-topic slots (CCampPages,
// ui_camp.h). See docs/notes/ui.md, section 5.
#include "ui_camp.h"

#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "originals_gameplay.h"
#include "originals_ui.h"
#include "originals_world.h"

namespace {

const char* GameString(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
const char* MoveSound() { return GameString(0x00793764); }   // "buttons\camp\move.wav"

// The help topics of pages 0-6 (the game's strings).
const char* PageHelpTopic(int page) {
    switch (page) {
    case 0: return GameString(0x0079371C);   // "camp_weapons"
    case 1: return GameString(0x00793710);   // "camp_skills"
    case 2: return GameString(0x007936FC);   // "camp_spell_trade"
    case 3: return GameString(0x007936E8);   // "camp_spell_constr"
    case 4: return GameString(0x007936D8);   // "camp_item_trade"
    case 5: return GameString(0x007936C4);   // "camp_item_constr"
    case 6: return GameString(0x007936B8);   // "camp_repair"
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
    InputOf(this)->BaseLButtonDown(buttons, x, y);
    int region = RegionAt(x, y);
    switch (region) {
    case kRegionAction:
        CampUpdateActionOriginal(this);
        if (actionEnabled_) CampDoActionOriginal(this);
        break;
    case kRegionSecondAction:
        CampUpdateActionOriginal(this);
        if (actionShown_) CampDoSecondActionOriginal(this);
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
            CampShowPageOriginal(this, nullptr, region - kRegionFirstPage);
        break;
    }
}

void CCampPages::OnLButtonUp(int buttons, int x, int y) {
    InputOf(this)->BaseLButtonUp(buttons, x, y);
    ReleaseMouse();
    turning_ = 0;
}

void CCampPages::OnKeyDown(int key, int repeat, int flags) {
    if (key == VK_ESCAPE) {
        CampShowPageOriginal(this, nullptr, kLeavePage);
        inputHandled_ = true;
        return;
    }
    if ((key == VK_BACK || key == VK_RETURN) && IsNetworkClientOriginal()) {
        if (key == VK_BACK)
            ChatKeyBackspaceOriginal(interface_);
        else
            ChatKeyEnterOriginal(interface_);
        inputHandled_ = true;
        return;
    }
    InputOf(this)->BaseKeyDown(key, repeat, flags);
}

void CCampPages::OnMessageResult(int result) {
    if (static_cast<std::uint32_t>(result) != kAnswerYes) return;
    PartyUnitRecord* hero = ShownHero(this);
    if (static_cast<int>(hero->weaponCount) <= 0) return;
    CEngineObject* weapon = hero->weapons[0];
    void* item = PartyAddItemOriginal(LocalPlayerData(), nullptr, weapon, 0, 1);
    RecordRemoveQuickEntryOriginal(hero, nullptr, weapon);   // 00660A50: the weapon list's Remove
    hero->weaponIndex = 0xFFFFFFFFu;
    ItemStripScrollToItemOriginal(itemStrip_, nullptr, item);
    CallVirtual(kSlotRefreshWeapons);
}
EI_REPLACE_METHOD(0x0060E900, CCampPages::OnMessageResult);

const char* CCampPages::HelpTopic() { return PageHelpTopic(page_); }
EI_REPLACE_METHOD(0x0060E980, CCampPages::HelpTopic);

namespace {
void __fastcall CampLButtonDown(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CCampPages>(input)->OnLButtonDown(buttons, x, y);
}
void __fastcall CampLButtonUp(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CCampPages>(input)->OnLButtonUp(buttons, x, y);
}
void __fastcall CampKeyDown(void* input, void*, int key, int repeat, int flags) {
    WindowFromInput<CCampPages>(input)->OnKeyDown(key, repeat, flags);
}
} // namespace
EI_REPLACE(0x0060F470, CampLButtonDown);
EI_REPLACE(0x0060F5B0, CampLButtonUp);
EI_REPLACE(0x0060F600, CampKeyDown);
