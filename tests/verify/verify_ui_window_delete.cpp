// Checks for the interface windows' deleting destructors, their input sides' copies and the short
// destructor bodies (ui_window_delete.cpp). All ours only (they free memory): a deleting destructor
// returns the object (input sides: the window, 0x94 before the side); after a body the window base
// holds nothing. Scenarios: every scenario closes windows (the main menu at a new game, ui_screens /
// ui_camp / ui_hero_screen / ui_saveload / ui_options close their screens and panels; the battle
// HUD's panels and the chat overlay go at exit).
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/ui_briefing.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_item_info.h"
#include "../../src/game/ui_screens.h"

#include <cstdint>

void __fastcall InputSideDestroy(UiInputSide* side);
UiInputSide* __fastcall InputSideDelete(UiInputSide* self, void*, unsigned flags);

namespace {
void* s_unused;

// The window base after its destructor body (as DestroyBaseVerify checks it).
bool BaseEmpty(const CUiWindow* w) {
    return !w->blackMaterial_ && !w->whiteMaterial_ && !w->unknown_0F4 && !w->objects_.data && !w->sceneObjects_.data &&
           !w->surfaces_.data && !w->regions_.data && !w->models_.data;
}

void CheckDeleted(const char* name, const void* self, const void* result, unsigned flags) {
    ei::verify::Check(name, true, result == self, "%p -> %p flags %u", self, result, flags);
}

} // namespace

// --- Bodies -----------------------------------------------------------------------------------------

static void __fastcall InputSideDestroyVerify(UiInputSide* side) {
    InputSideDestroy(side);
    ei::verify::Check("InputSideDestroy", true, side->vtable_ == reinterpret_cast<void**>(0x007401D0), "");
}
EI_VERIFY(0x005EACF0, InputSideDestroyVerify, 0, s_unused);
EI_VERIFY(0x00602BA0, InputSideDestroyVerify, 0, s_unused);

static void __fastcall ItemInfoPanelDestroyVerify(CItemInfoPanel* self) {
    self->DestroyBody();
    ei::verify::Check("CItemInfoPanel::DestroyBody", true,
                      BaseEmpty(self) && self->vtable_ == reinterpret_cast<void**>(0x007404DC), "");
}
EI_VERIFY(0x005EE380, ItemInfoPanelDestroyVerify, 0, s_unused);

static void __fastcall ChatWindowDestroyVerify(CChatWindow* self) {
    self->DestroyBody();
    ei::verify::Check("CChatWindow::DestroyBody", true, BaseEmpty(self), "");
}
EI_VERIFY(0x00608ED0, ChatWindowDestroyVerify, 0, s_unused);

static void __fastcall ForwardingStripDestroyVerify(CUiItemStrip* self) {
    self->DestroyForwarding();
    ei::verify::Check("CUiItemStrip::DestroyForwarding", true,
                      BaseEmpty(&self->window) && self->list_.vtable == reinterpret_cast<void*>(0x00741BC0), "");
}
EI_VERIFY(0x0060E2B0, ForwardingStripDestroyVerify, 0, s_unused);

static void __fastcall SpellMakerDestroyVerify(CSpellMakerPage* self) {
    self->DestroyBody();
    ei::verify::Check("CSpellMakerPage::DestroyBody", true, BaseEmpty(self), "");
}
EI_VERIFY(0x0060E600, SpellMakerDestroyVerify, 0, s_unused);

static void __fastcall MessageBoxDestroyVerify(CMessageBox* self) {
    self->DestroyBody();
    ei::verify::Check("CMessageBox::DestroyBody", true, BaseEmpty(self) && !self->children_.data, "");
}
EI_VERIFY(0x0061E3E0, MessageBoxDestroyVerify, 0, s_unused);
EI_VERIFY(0x00633750, MessageBoxDestroyVerify, 0, s_unused);

static void __fastcall MasterRequestBoxDestroyVerify(CMasterRequestBox* self) {
    self->DestroyBody();
    ei::verify::Check("CMasterRequestBox::DestroyBody", true, BaseEmpty(self) && !self->children_.data, "");
}
EI_VERIFY(0x0061E460, MasterRequestBoxDestroyVerify, 0, s_unused);

static void __fastcall ChatLineDestroyVerify(ChatLine* self) {
    self->Destroy();
    ei::verify::Check("ChatLine::Destroy", true, self->surface == nullptr, "");
}
EI_VERIFY(0x005E2670, ChatLineDestroyVerify, 0, s_unused);

static void __fastcall ChatOverlayDestroyVerify(CChatOverlay* self) {
    self->DestroyBody();
    ei::verify::Check("CChatOverlay::DestroyBody", true, BaseEmpty(self), "");
}
EI_VERIFY(0x005E28E0, ChatOverlayDestroyVerify, 0, s_unused);

// The containers below: the base empty and no children left.
static void __fastcall BattleScreenSetupDestroyVerify(CBattleScreenSetup* self) {
    self->DestroyBody();
    ei::verify::Check("CBattleScreenSetup::DestroyBody", true,
                      BaseEmpty(self) && !self->children_.data && self->personTextCount_ == 0, "");
}
EI_VERIFY(0x005F6130, BattleScreenSetupDestroyVerify, 0, s_unused);

static void __fastcall BriefingDestroyVerify(CBriefingScreen* self) {
    self->DestroyBody();
    ei::verify::Check("CBriefingScreen::DestroyBody", true,
                      BaseEmpty(self) && !self->children_.data && self->personTextCount_ == 0, "");
}
EI_VERIFY(0x005F6BA0, BriefingDestroyVerify, 0, s_unused);

static void __fastcall ConnectScreenDestroyVerify(CMpConnectScreen* self) {
    self->DestroyBody();
    ei::verify::Check("CMpConnectScreen::DestroyBody", true, BaseEmpty(self) && !self->children_.data, "");
}
EI_VERIFY(0x00618570, ConnectScreenDestroyVerify, 0, s_unused);

static void __fastcall HelpWindowDestroyVerify(CHelpWindow* self) {
    self->DestroyBody();
    ei::verify::Check("CHelpWindow::DestroyBody", true, BaseEmpty(self) && !self->children_.data, "");
}
EI_VERIFY(0x005F8970, HelpWindowDestroyVerify, 0, s_unused);

static void __fastcall ObjectivesDestroyVerify(CObjectivesWindow* self) {
    self->DestroyBody();
    ei::verify::Check("CObjectivesWindow::DestroyBody", true,
                      BaseEmpty(self) && !self->children_.data && self->exitsVtable_ == reinterpret_cast<void*>(0x00741264), "");
}
EI_VERIFY(0x00603B90, ObjectivesDestroyVerify, 0, s_unused);

static void __fastcall SaveLoadDestroyVerify(CSaveLoadWindow* self) {
    self->DestroyBody();
    ei::verify::Check("CSaveLoadWindow::DestroyBody", true, BaseEmpty(self) && !self->children_.data, "");
}
EI_VERIFY(0x006070C0, SaveLoadDestroyVerify, 0, s_unused);

// --- Deleting destructors -----------------------------------------------------------------------------

#define EI_DELETE_VERIFY(Type, Fn)                                                       \
    Type* __fastcall Fn(Type* self, void*, unsigned flags);                              \
    static Type* __fastcall Fn##Verify(Type* self, void*, unsigned flags) {              \
        Type* result = Fn(self, nullptr, flags);                                         \
        CheckDeleted(#Fn, self, result, flags);                                          \
        return result;                                                                   \
    }

EI_DELETE_VERIFY(CUiWindow, UiWindowDelete)
EI_DELETE_VERIFY(CUiContainer, UiContainerDelete)
EI_DELETE_VERIFY(CChatOverlay, ChatOverlayDelete)
EI_DELETE_VERIFY(CItemInfoPanel, ItemInfoPanelDelete)
EI_DELETE_VERIFY(CBattleScreenSetup, BattleScreenSetupDelete)
EI_DELETE_VERIFY(CItemScreen, ItemScreenDelete)
EI_DELETE_VERIFY(CItemScreen, CampScreenDelete)
EI_DELETE_VERIFY(CItemScreen, Screen9Delete)
EI_DELETE_VERIFY(CGlobalMap, GlobalMapDelete)
EI_DELETE_VERIFY(CBriefingScreen, BriefingDelete)
EI_DELETE_VERIFY(void, Screen8Delete)
EI_DELETE_VERIFY(CHelpWindow, HelpWindowDelete)
EI_DELETE_VERIFY(CObjectivesWindow, ObjectivesDelete)
EI_DELETE_VERIFY(CSaveLoadWindow, SaveLoadDelete)
EI_DELETE_VERIFY(CChatWindow, ChatWindowDelete)
EI_DELETE_VERIFY(CUiItemStrip, ForwardingStripDelete)
EI_DELETE_VERIFY(CUiWindow, TradePanelDelete)
EI_DELETE_VERIFY(CSpellMakerPage, SpellMakerDelete)
EI_DELETE_VERIFY(CMpConnectScreen, ConnectScreenDelete)
EI_DELETE_VERIFY(CMasterRequestBox, MasterRequestBoxDelete)
EI_DELETE_VERIFY(COptionsWindow, OptionsWindowDelete)
EI_DELETE_VERIFY(CMpHeroScreen, HeroScreenDelete)
EI_DELETE_VERIFY(CMessageBox, MessageBoxDelete)
EI_DELETE_VERIFY(CHudMap, HudMapDelete)
EI_DELETE_VERIFY(CHudUnitPanel, HudUnitPanelDelete)
EI_DELETE_VERIFY(CHudTopPanel, HudTopPanelDelete)
EI_DELETE_VERIFY(CHudWeaponBar, HudWeaponBarDelete)
EI_DELETE_VERIFY(CHudQuickItems, HudQuickItemsDelete)
EI_DELETE_VERIFY(CHudSpellColumn, HudSpellColumnDelete)
EI_DELETE_VERIFY(CHudActionColumn, HudActionColumnDelete)
EI_DELETE_VERIFY(CHudPortraits, HudPortraitsDelete)

static UiInputSide* __fastcall InputSideDeleteVerify(UiInputSide* self, void*, unsigned flags) {
    UiInputSide* result = InputSideDelete(self, nullptr, flags);
    CheckDeleted("InputSideDelete", self, result, flags);
    return result;
}

EI_VERIFY(0x005FE910, UiWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x005EE020, UiWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060E240, UiWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060E260, UiWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060E2E0, UiWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060E310, UiWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060E340, UiWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060E5B0, UiWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060E650, UiWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060E680, UiWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x006157D0, UiWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x00635D80, UiWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x00635DB0, UiWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x00600B80, UiContainerDeleteVerify, 0, s_unused);
EI_VERIFY(0x005F5F30, UiContainerDeleteVerify, 0, s_unused);
EI_VERIFY(0x005F5F60, UiContainerDeleteVerify, 0, s_unused);
EI_VERIFY(0x00603CD0, UiContainerDeleteVerify, 0, s_unused);
EI_VERIFY(0x005EACD0, InputSideDeleteVerify, 0, s_unused);
EI_VERIFY(0x00602B80, InputSideDeleteVerify, 0, s_unused);
EI_VERIFY(0x005E28C0, ChatOverlayDeleteVerify, 0, s_unused);
EI_VERIFY(0x005EE420, ItemInfoPanelDeleteVerify, 0, s_unused);
EI_VERIFY(0x005F5FC0, BattleScreenSetupDeleteVerify, 0, s_unused);
EI_VERIFY(0x005F6450, ItemScreenDeleteVerify, 0, s_unused);
EI_VERIFY(0x005F6600, CampScreenDeleteVerify, 0, s_unused);
EI_VERIFY(0x005F6FF0, Screen9DeleteVerify, 0, s_unused);
EI_VERIFY(0x005F6900, GlobalMapDeleteVerify, 0, s_unused);
EI_VERIFY(0x005F6B80, BriefingDeleteVerify, 0, s_unused);
EI_VERIFY(0x005F6E20, Screen8DeleteVerify, 0, s_unused);
EI_VERIFY(0x005F8950, HelpWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x00603B70, ObjectivesDeleteVerify, 0, s_unused);
EI_VERIFY(0x006070A0, SaveLoadDeleteVerify, 0, s_unused);
EI_VERIFY(0x00608EB0, ChatWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060E290, ForwardingStripDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060E3C0, TradePanelDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060E5E0, SpellMakerDeleteVerify, 0, s_unused);
EI_VERIFY(0x00618550, ConnectScreenDeleteVerify, 0, s_unused);
EI_VERIFY(0x0061E440, MasterRequestBoxDeleteVerify, 0, s_unused);
EI_VERIFY(0x0062B160, OptionsWindowDeleteVerify, 0, s_unused);
EI_VERIFY(0x00633240, HeroScreenDeleteVerify, 0, s_unused);
EI_VERIFY(0x00633730, MessageBoxDeleteVerify, 0, s_unused);
EI_VERIFY(0x00634770, MessageBoxDeleteVerify, 0, s_unused);
EI_VERIFY(0x00635C80, HudMapDeleteVerify, 0, s_unused);
EI_VERIFY(0x00635CE0, HudUnitPanelDeleteVerify, 0, s_unused);
EI_VERIFY(0x00635DE0, HudTopPanelDeleteVerify, 0, s_unused);
EI_VERIFY(0x0063BC50, HudWeaponBarDeleteVerify, 0, s_unused);
EI_VERIFY(0x0063CA50, HudQuickItemsDeleteVerify, 0, s_unused);
EI_VERIFY(0x0063E440, HudSpellColumnDeleteVerify, 0, s_unused);
EI_VERIFY(0x0063F470, HudActionColumnDeleteVerify, 0, s_unused);
EI_VERIFY(0x0063FF50, HudPortraitsDeleteVerify, 0, s_unused);

// --- Input sides: the result is the window ----------------------------------------------------------

#define EI_INPUT_DELETE_VERIFY(Fn)                                                       \
    void* __fastcall Fn(void* input, void*, unsigned flags);                             \
    static void* __fastcall Fn##Verify(void* input, void*, unsigned flags) {             \
        void* result = Fn(input, nullptr, flags);                                        \
        CheckDeleted(#Fn, static_cast<char*>(input) - 0x94, result, flags);              \
        return result;                                                                   \
    }

EI_INPUT_DELETE_VERIFY(ChatOverlayInputDelete)
EI_INPUT_DELETE_VERIFY(UiWindowInputDelete)
EI_INPUT_DELETE_VERIFY(UiContainerInputDelete)
EI_INPUT_DELETE_VERIFY(ItemInfoPanelInputDelete)
EI_INPUT_DELETE_VERIFY(BattleScreenSetupInputDelete)
EI_INPUT_DELETE_VERIFY(ItemScreenInputDelete)
EI_INPUT_DELETE_VERIFY(CampScreenInputDelete)
EI_INPUT_DELETE_VERIFY(Screen9InputDelete)
EI_INPUT_DELETE_VERIFY(GlobalMapInputDelete)
EI_INPUT_DELETE_VERIFY(BriefingInputDelete)
EI_INPUT_DELETE_VERIFY(Screen8InputDelete)
EI_INPUT_DELETE_VERIFY(HelpWindowInputDelete)
EI_INPUT_DELETE_VERIFY(ObjectivesInputDelete)
EI_INPUT_DELETE_VERIFY(SaveLoadInputDelete)
EI_INPUT_DELETE_VERIFY(ChatWindowInputDelete)
EI_INPUT_DELETE_VERIFY(ForwardingStripInputDelete)
EI_INPUT_DELETE_VERIFY(TradePanelInputDelete)
EI_INPUT_DELETE_VERIFY(SpellMakerInputDelete)
EI_INPUT_DELETE_VERIFY(ConnectScreenInputDelete)
EI_INPUT_DELETE_VERIFY(MasterRequestBoxInputDelete)
EI_INPUT_DELETE_VERIFY(OptionsWindowInputDelete)
EI_INPUT_DELETE_VERIFY(HeroScreenInputDelete)
EI_INPUT_DELETE_VERIFY(MessageBoxInputDelete)

EI_VERIFY(0x005E3C80, ChatOverlayInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00600AC0, UiWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x005F45F0, UiWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00613D50, UiWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00613D60, UiWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00613D70, UiWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00613D80, UiWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00613D90, UiWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00613DA0, UiWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00613DD0, UiWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00613DE0, UiWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0061B210, UiWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00601E70, UiContainerInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x005FC140, UiContainerInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x005FC120, UiContainerInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060C7E0, UiContainerInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x005F4600, ItemInfoPanelInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x005FC0E0, BattleScreenSetupInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x005FC0C0, ItemScreenInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x005FC100, CampScreenInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x005FC110, Screen9InputDeleteVerify, 0, s_unused);
EI_VERIFY(0x005FC130, GlobalMapInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x005FC0B0, BriefingInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x005FC0F0, Screen8InputDeleteVerify, 0, s_unused);
EI_VERIFY(0x005FC0D0, HelpWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060C7C0, ObjectivesInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060C7B0, SaveLoadInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0060C7D0, ChatWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00613DC0, ForwardingStripInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00613DB0, TradePanelInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00613DF0, SpellMakerInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0061B220, ConnectScreenInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00620300, MasterRequestBoxInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0062B770, OptionsWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x006345F0, HeroScreenInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x006345E0, MessageBoxInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x00634FD0, MessageBoxInputDeleteVerify, 0, s_unused);

// The battle HUD panels' input sides (every game scenario at exit).
EI_INPUT_DELETE_VERIFY(HudWeaponBarInputDelete)
EI_INPUT_DELETE_VERIFY(HudQuickItemsInputDelete)
EI_INPUT_DELETE_VERIFY(HudSpellColumnInputDelete)
EI_INPUT_DELETE_VERIFY(HudActionColumnInputDelete)
EI_INPUT_DELETE_VERIFY(HudPortraitsInputDelete)
EI_INPUT_DELETE_VERIFY(HudUnitPanelInputDelete)
EI_INPUT_DELETE_VERIFY(HudMapInputDelete)
EI_INPUT_DELETE_VERIFY(HudTopPanelInputDelete)
EI_VERIFY(0x0064BCF0, HudWeaponBarInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0064BD00, HudQuickItemsInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0064BD10, HudSpellColumnInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0064BD20, HudActionColumnInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0064BD30, HudPortraitsInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0064BD40, HudUnitPanelInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0064BD50, HudMapInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0064BD60, UiWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0064BD70, UiWindowInputDeleteVerify, 0, s_unused);
EI_VERIFY(0x0064BD80, HudTopPanelInputDeleteVerify, 0, s_unused);
