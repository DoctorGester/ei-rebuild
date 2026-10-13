// Checks for the small interface members ported in A-I22 (src/game/ui_small_members.cpp, the
// exchange's slots 42 / 43 in ui_exchange.cpp and the titles' slot 27 in ui_credits.cpp). All ours
// only: each check is the function's whole effect read back (they are a few instructions each; a
// side by side run would compare the same stores).
//
// Which scenario reaches each:
// 005F6080 / 005F5F90: every battle screen made (CreateScreen 1; any scenario with newgame).
// 00632490 / 006324E0, 0062EE70, 0062FEF0: the global map (screen 4; ui_camp); 006316A0 only from
//   the Rebirth-patched click 0062F950 (a click on a zone; ui_camp in the vanilla launch).
// 0062D100 / 0062D200: the exchange between players; multiplayer only, no scenario reaches them.
// 005E28B0: a wheel turn over a window without a wheel handler (ui_container_input).
// 005EE400, 005F65F0, 00621F20, 00625510, 0060F690, 00624A40, 005EE720: the camp (ui_camp,
//   ui_camp_deals, ui_trade_hold, ui_describe).
// 005F6380 / 005F63A0 / 005F6400 / 005F6430 / 005F6440: the plain item screen's and the exchange's
//   slots; 005F63A0, 005F6400 and 005F6440 also the hero screen's (ui_hero_screen).
// 006157C0: the figure strips of the hero screen (ui_hero_screen).
// 005F5FB0: the battle screen's slot 38 (multiplayer portrait pick; no scenario). 005F6B70: the
//   briefing's slot 38 (the chat window, multiplayer; no scenario).
// 005E7090: the quick items' bar (ui_quick_items). 005E70B0: no direct caller found; no scenario.
// 005E6990: CInterface3D::Create (Rebirth-patched; every vanilla launch).
// 005E6D70: a sprite destroyed (every screen closed). 005ED760: slot 2 of an item's turning state
//   (ui_camp). 005EC010 / 005EC2F0: an item screen destroyed (ui_camp).
// 005E3FC0: the console shown while the interface redraws (console; Rebirth-patched caller).
// 005F7C60 / 005F7C80: Enter / Backspace on a multiplayer client's screens; no scenario.
// 0062ACB0, 00634D00: the escape menu and its quit box closed (ui_screens). 0062BFD0: a window
//   closed over the titles (render_titles_present does not open one; no scenario).
#include "verify.h"

#include "../../src/game/console.h"
#include "../../src/game/ui.h"
#include "../../src/game/ui_briefing.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_item_info.h"
#include "../../src/game/ui_screens.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

bool SameText(const char* a, const char* b) { return std::strcmp(a, b) == 0; }
} // namespace

static CBattleScreenSetup::PartyIds* __fastcall PartyIdsConstructVerify(CBattleScreenSetup::PartyIds* self) {
    CBattleScreenSetup::PartyIds* result = self->Construct();
    bool ok = result == self && !self->withUnit.begin && !self->withUnit.end && !self->withUnit.capacity &&
              !self->without.begin && !self->without.end && !self->without.capacity;
    ei::verify::Check("CBattleScreenSetup::PartyIds::Construct", true, ok, "");
    return result;
}
EI_VERIFY(0x005F6080, PartyIdsConstructVerify, 0, s_unused);

static BattleTargetQuery* __fastcall TargetQueryConstructVerify(BattleTargetQuery* self) {
    BattleTargetQuery* result = self->Construct();
    bool ok = result == self && self->kind == 1 && self->unknown_4 == 0 && self->unknown_8 == 0;
    ei::verify::Check("BattleTargetQuery::Construct", true, ok, "");
    return result;
}
EI_VERIFY(0x005F5F90, TargetQueryConstructVerify, 0, s_unused);

namespace {
bool SamePlace(const GlobalMapPlace& a, const GlobalMapPlace& b) {
    return a.zone == b.zone && std::memcmp(&a.position, &b.position, sizeof(a.position)) == 0 &&
           SameText(a.name, b.name) && SameText(a.title, b.title);
}
} // namespace

static GlobalMapPlace* __fastcall PlaceAssignVerify(GlobalMapPlace* self, void*, const GlobalMapPlace* from) {
    GlobalMapPlace* result = self->Assign(*from);
    ei::verify::Check("GlobalMapPlace::Assign", true, result == self && SamePlace(*self, *from), "zone %d", from->zone);
    return result;
}
EI_VERIFY(0x00632490, PlaceAssignVerify, 0, s_unused);

static GlobalMapPlace* __fastcall PlaceAssignCopyVerify(GlobalMapPlace* self, void*, const GlobalMapPlace* from) {
    GlobalMapPlace* result = self->AssignCopy(*from);
    ei::verify::Check("GlobalMapPlace::AssignCopy", true, result == self && SamePlace(*self, *from), "zone %d",
                      from->zone);
    return result;
}
EI_VERIFY(0x006324E0, PlaceAssignCopyVerify, 0, s_unused);

static void __fastcall ExchangeMoveOfferedVerify(CExchangeScreen* self, void*, CItem* item, int mode) {
    self->MoveOffered(item, mode);
    ei::verify::Check("CExchangeScreen::MoveOffered", true, true, "mode %d", mode);
}
EI_VERIFY(0x0062D100, ExchangeMoveOfferedVerify, 0, s_unused);

static bool __fastcall ExchangeCanMoveVerify(CExchangeScreen* self, void*, CItem* item, int mode) {
    bool result = self->CanMove(item, mode);
    ei::verify::Check("CExchangeScreen::CanMove", true, result, "mode %d", mode);
    return result;
}
EI_VERIFY(0x0062D200, ExchangeCanMoveVerify, 0, s_unused);

// --- Default answers -----------------------------------------------------------------------------------

static int __fastcall WheelIgnoredVerify(UiInputSide* self, void*, int flags, int delta, int x, int y) {
    int result = self->WheelIgnored(flags, delta, x, y);
    ei::verify::Check("UiInputSide::WheelIgnored", 0, result, "");
    return result;
}
EI_VERIFY(0x005E28B0, WheelIgnoredVerify, 0, s_unused);

static void* __fastcall NoItemAtVerify(CUiWindow* self, void*, int x, int y, int* topic) {
    void* result = self->NoItemAt(x, y, topic);
    ei::verify::Check("CUiWindow::NoItemAt", true, result == nullptr && *topic == 0, "");
    return result;
}
EI_VERIFY(0x005EE400, NoItemAtVerify, 0, s_unused);

static bool __fastcall CanEquipDefaultVerify(CItemScreen* self, void*, CItem* item, int mode) {
    bool result = self->CanEquipDefault(item, mode);
    ei::verify::Check("CItemScreen::CanEquipDefault", false, result, "");
    return result;
}
EI_VERIFY(0x005F6380, CanEquipDefaultVerify, 0, s_unused);

static void __fastcall BestPartySkillDefaultVerify(CItemScreen* self, void*, int school, int* skill, int* mana) {
    self->BestPartySkillDefault(school, skill, mana);
    ei::verify::Check("CItemScreen::BestPartySkillDefault", true, *skill == 0 && *mana == 0, "");
}
EI_VERIFY(0x005F63A0, BestPartySkillDefaultVerify, 0, s_unused);

static void* __fastcall HeroUnitDefaultVerify(CItemScreen* self, void*, int index) {
    void* result = self->HeroUnitDefault(index);
    ei::verify::Check("CItemScreen::HeroUnitDefault", true, result == nullptr, "");
    return result;
}
EI_VERIFY(0x005F6400, HeroUnitDefaultVerify, 0, s_unused);

static float __fastcall WeightDefaultVerify(CItemScreen* self) {
    float result = self->WeightDefault();
    ei::verify::Check("CItemScreen::WeightDefault", true, result == 0.0f, "%f", double(result));
    return result;
}
EI_VERIFY(0x005F6430, WeightDefaultVerify, 0, s_unused);

static int __fastcall PriceDefaultVerify(CItemScreen* self, void*, CItem* item, int kind) {
    int result = self->PriceDefault(item, kind);
    ei::verify::Check("CItemScreen::PriceDefault", 0, result, "");
    return result;
}
EI_VERIFY(0x005F6440, PriceDefaultVerify, 0, s_unused);

static void* __fastcall FigureItemAtVerify(CUiItemStrip* self, void*, int x, int y, int* topic) {
    void* result = self->FigureItemAt(x, y, topic);
    ei::verify::Check("CUiItemStrip::FigureItemAt", true, result == nullptr, "");
    return result;
}
EI_VERIFY(0x006157C0, FigureItemAtVerify, 0, s_unused);

static bool __fastcall MayTravelVerify(CGlobalMap* self, void*, int unused) {
    bool result = self->MayTravel(unused);
    ei::verify::Check("CGlobalMap::MayTravel", true, result, "");
    return result;
}
EI_VERIFY(0x006316A0, MayTravelVerify, 0, s_unused);

static const char* __fastcall GlobalMapHelpTopicVerify(CGlobalMap* self) {
    const char* result = self->HelpTopic();
    ei::verify::Check("CGlobalMap::HelpTopic", true,
                      SameText(result, self->noCamp_ == 1 ? "global_map_nocamp" : "global_map"), "%s", result);
    return result;
}
EI_VERIFY(0x0062EE70, GlobalMapHelpTopicVerify, 0, s_unused);

// --- Setters and readers -------------------------------------------------------------------------------

static void __fastcall SetPortraitModeVerify(CBattleScreenSetup* self, void*, int mode) {
    self->SetPortraitMode(mode);
    ei::verify::Check("CBattleScreenSetup::SetPortraitMode", mode, self->portraitMode_, "");
}
EI_VERIFY(0x005F5FB0, SetPortraitModeVerify, 0, s_unused);

static void __fastcall SetChatActiveVerify(CBriefingScreen* self, void*, int mode) {
    self->SetChatActive(mode);
    ei::verify::Check("CBriefingScreen::SetChatActive", mode, self->chatActive_, "");
}
EI_VERIFY(0x005F6B70, SetChatActiveVerify, 0, s_unused);

static std::uint8_t* __fastcall CampHeroAtVerify(CCampPages* self, void*, int index) {
    std::uint8_t* result = self->HeroAt(index);
    ei::verify::Check("CCampPages::HeroAt", true, result == self->heroes_[index], "index %d", index);
    return result;
}
EI_VERIFY(0x005F65F0, CampHeroAtVerify, 0, s_unused);

static void __fastcall StripSetSlotsVerify(CUiItemStrip* self, void*, int slots) {
    self->SetSlots(slots);
    ei::verify::Check("CUiItemStrip::SetSlots", slots, self->slots_, "");
}
EI_VERIFY(0x00621F20, StripSetSlotsVerify, 0, s_unused);

static std::uint8_t __fastcall TradeBoughtVerify(const CTradePanel* self) {
    std::uint8_t result = self->Bought();
    ei::verify::Check("CTradePanel::Bought", int(self->Record().bought), int(result), "record %d", self->recordIndex_);
    return result;
}
EI_VERIFY(0x00625510, TradeBoughtVerify, 0, s_unused);

static CItem* __fastcall InfoPanelFirstItemVerify(const CItemInfoPanel* self) {
    CItem* result = self->FirstItem();
    ei::verify::Check("CItemInfoPanel::FirstItem", true, result == self->Item(), "");
    return result;
}
EI_VERIFY(0x005EE720, InfoPanelFirstItemVerify, 0, s_unused);

static void __fastcall MeshSetScaleYVerify(CUiMesh* self, void*, std::uint32_t bits) {
    float before[16];
    std::memcpy(before, self->scaleMatrix_, sizeof(before));
    self->SetScaleYBits(bits);
    std::memcpy(&before[5], &bits, sizeof(bits));
    ei::verify::Check("CUiMesh::SetScaleYBits", true, std::memcmp(before, self->scaleMatrix_, sizeof(before)) == 0, "");
}
EI_VERIFY(0x005E7090, MeshSetScaleYVerify, 0, s_unused);

static float __fastcall MeshGetScaleXVerify(const CUiMesh* self) {
    float result = self->GetScaleX();
    ei::verify::Check("CUiMesh::GetScaleX", true, std::memcmp(&result, &self->scaleMatrix_[0], sizeof(float)) == 0, "");
    return result;
}
EI_VERIFY(0x005E70B0, MeshGetScaleXVerify, 0, s_unused);

// --- Constructors and destructors ------------------------------------------------------------------------

static CScreenFade* __fastcall FadeConstructVerify(CScreenFade* self) {
    CScreenFade* result = self->Construct();
    bool ok = result == self && self->alpha_ == 0.0f && self->active_ == 0 && self->letterbox_ == 0;
    ei::verify::Check("CScreenFade::Construct", true, ok, "");
    return result;
}
EI_VERIFY(0x005E6990, FadeConstructVerify, 0, s_unused);

// 005F46A0: the interface object's static initialiser runs before the hook is installed, so no run
// reaches this wrapper.
static CInterface3D* __fastcall InterfaceConstructVerify(CInterface3D* self) {
    CInterface3D* result = self->Construct();
    bool ok = result == self && !self->stackBegin_ && !self->stackEnd_ && !self->stackCapacity_ &&
              self->messages_.Empty() && self->helpTopics_.size() == 0 && self->messageText_.GetLength() == 0 &&
              self->savedText_.GetLength() == 0;
    ei::verify::Check("CInterface3D::Construct", true, ok, "");
    return result;
}
EI_VERIFY(0x005F46A0, InterfaceConstructVerify, 0, s_unused);

static int __fastcall MeshDestroyVerify(CUiMesh* self) {
    int result = self->DestroySprite();
    ei::verify::Check("CUiMesh::DestroySprite", true, !self->material_ && !self->texture_, "");
    return result;
}
EI_VERIFY(0x005E6D70, MeshDestroyVerify, 0, s_unused);

static void __fastcall ItemTurnResetVerify(ItemTurn* self) {
    self->ResetVtable();
    ei::verify::Check("ItemTurn::ResetVtable", true, self->vtable == reinterpret_cast<void*>(std::uintptr_t(0x007403F8)),
                      "");
}
EI_VERIFY(0x005ED760, ItemTurnResetVerify, 0, s_unused);

// The release may free the object: nothing is read back.
static int __fastcall ReleaseModelVerify(void* object) {
    int result = UiReleaseCachedModel(object);
    ei::verify::Check("UiReleaseCachedModel", true, true, "");
    return result;
}
EI_VERIFY(0x005EC010, ReleaseModelVerify, 0, s_unused);

static int __fastcall ReleaseTextureVerify(void* object) {
    int result = UiReleaseCachedTexture(object);
    ei::verify::Check("UiReleaseCachedTexture", true, true, "");
    return result;
}
EI_VERIFY(0x005EC2F0, ReleaseTextureVerify, 0, s_unused);

// --- Handlers --------------------------------------------------------------------------------------------

static int __fastcall ConsoleBlitVerify(console::ConsoleWindow* self) {
    int result = self->Blit();
    ei::verify::Check("ConsoleWindow::Blit", true, true, "surface %d", self->surface_ != nullptr);
    return result;
}
EI_VERIFY(0x005E3FC0, ConsoleBlitVerify, 0, s_unused);

static void __fastcall ChatKeyEnterVerify(CInterface3D* self) {
    self->ChatKeyEnter();
    ei::verify::Check("CInterface3D::ChatKeyEnter", true, self->root_->chatOpen_, "");
}
EI_VERIFY(0x005F7C60, ChatKeyEnterVerify, 0, s_unused);

static void __fastcall ChatKeyBackspaceVerify(CInterface3D* self) {
    bool open = self->root_->chatOpen_;
    self->ChatKeyBackspace();
    ei::verify::Check("CInterface3D::ChatKeyBackspace", open, self->root_->chatOpen_, "");
}
EI_VERIFY(0x005F7C80, ChatKeyBackspaceVerify, 0, s_unused);

static void __fastcall CampKeyUpVerify(void* input, void*, int key, int repeat, int flags) {
    WindowFromInput<CCampPages>(input)->OnKeyUp(key, repeat, flags);
    ei::verify::Check("CCampPages::OnKeyUp", true, true, "key %d", key);
}
EI_VERIFY(0x0060F690, CampKeyUpVerify, 0, s_unused);

static int __fastcall StatsPanelLButtonUpVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CHeroStatsPanel>(input);
    int result = self->OnLButtonUp(buttons, x, y);
    bool ok = result == 0 && !self->interface_->mouseCapture_ && self->heldRegion_ == -1 && self->holdTime_ == 0.0f;
    ei::verify::Check("CHeroStatsPanel::OnLButtonUp", true, ok, "");
    return result;
}
EI_VERIFY(0x00624A40, StatsPanelLButtonUpVerify, 0, s_unused);

static int __fastcall GlobalMapLButtonUpVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CGlobalMap>(input);
    int turning = self->turning_;
    int pending = self->selectPending_;
    int result = self->OnLButtonUp(buttons, x, y);
    bool ok = result == 0 && self->turning_ == 0 && self->selectPending_ == (turning ? 1 : pending);
    ei::verify::Check("CGlobalMap::OnLButtonUp", true, ok, "turning %d", turning);
    return result;
}
EI_VERIFY(0x0062FEF0, GlobalMapLButtonUpVerify, 0, s_unused);

static void __fastcall EscMenuCloseVerify(CEscMenu* self) {
    self->OnClose();
    ei::verify::Check("CEscMenu::OnClose", true, true, "");
}
EI_VERIFY(0x0062ACB0, EscMenuCloseVerify, 0, s_unused);

static void __fastcall CreditsMessageVerify(CCreditsScreen* self, void*, int result) {
    self->OnMessageResult(result);
    ei::verify::Check("CCreditsScreen::OnMessageResult", true, true, "");
}
EI_VERIFY(0x0062BFD0, CreditsMessageVerify, 0, s_unused);

static void __fastcall MessageBoxCloseVerify(CMessageBox* self) {
    self->OnClose();
    ei::verify::Check("CMessageBox::OnClose", true, self->sceneObjects_.size == 0 && self->surfaces_.size == 0,
                      "objects %d surfaces %d", self->sceneObjects_.size, self->surfaces_.size);
}
EI_VERIFY(0x00634D00, MessageBoxCloseVerify, 0, s_unused);
