// Slot 0 of the interface's windows: the scalar deleting destructors (the destructor body, then the
// memory freed when bit 0 of the flags is set; they return the object), the input sides' copies
// of them (input vtable slot 0: `this` moved back from the input side at +0x94 to the window,
// then a jump to the window's), and the short destructor bodies only they call. Bodies that only
// jump to the window's or container's destructor are registered as those. docs/notes/ui.md,
// section 2 "Deleting destructors".
#include "global_map_clouds.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_create_screen.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include "core_refcount.h"
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_gameplay.h"
#include "originals_library.h"

namespace {

// Destructor bodies not rewritten yet.
// 00440D40: the master server link's destructor (net_socket_driver.cpp).
__declspec(naked) void __fastcall MasterLinkDestructAt(void* /*link*/) EI_ORIGINAL(0x00440D40)
// 005FAFA0 (RET 4): the "pers" texts tree's _M_erase(node) (library code).
__declspec(naked) void __fastcall PersonTextsEraseAt(PersonTextMap*, void*, PersonTextNode*) EI_ORIGINAL(0x005FAFA0)

// The vtables the bodies write.
void** InputSideBaseVtable() { return reinterpret_cast<void**>(0x007401D0); }
void** ItemInfoPanelVtable() { return reinterpret_cast<void**>(0x007404DC); }
void** ItemInfoPanelInputVtable() { return reinterpret_cast<void**>(0x007404A8); }
void* ItemArrayVtable() { return reinterpret_cast<void*>(0x00741BC0); }        // CArray<char*>
void* PartyArrayVtable() { return reinterpret_cast<void*>(0x007402AC); }       // the battle screen's party
void* DwordArrayVtable() { return reinterpret_cast<void*>(0x0073C3D0); }       // CDWordArray
void* ObjectiveExitsVtable() { return reinterpret_cast<void*>(0x00741264); }   // CArray<ObjectiveExit>

// An STLport vector's storage back to the node pool (its elements already destroyed).
template <typename T>
void FreeVectorStorage(T* begin, T* capacity) {
    stlp::deallocate(begin, static_cast<std::size_t>(capacity - begin) * sizeof(T));
}

// A "pers" texts map destroyed as the screens' destructors inline it: with nodes, every node
// erased (005FAFA0) and the header reset, the count 0; then the header freed.
void DestroyPersonTexts(PersonTextMap& map, std::uint32_t& count) {
    PersonTextNode* header = map.header;
    if (count != 0) {
        PersonTextsEraseAt(&map, nullptr, header->parent);
        header->left = header;
        header->parent = nullptr;
        header->right = header;
        count = 0;
    }
    stlp::deallocate(map.header, sizeof(PersonTextNode));
}

// An STLport list emptied and its sentinel freed (the destructor's inlined clear).
template <typename T, typename F>
void DestroyList(stlp::list<T>& list, F destroy) {
    list.ClearWith(destroy);
    stlp::deallocate(list.head, sizeof(typename stlp::list<T>::Node));
}

// The deleting destructors' common tail.
template <typename T>
T* FreeIfAsked(T* self, unsigned flags) {
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

} // namespace

// --- Destructor bodies -------------------------------------------------------------------------

// 005EACF0 / 00602BA0: an input side's (the battle camera's 00740208, the inert extra side
// 00741154): the base input vtable back.
void __fastcall InputSideDestroy(UiInputSide* side) { side->vtable_ = InputSideBaseVtable(); }
EI_REPLACE(0x005EACF0, InputSideDestroy);
EI_REPLACE(0x00602BA0, InputSideDestroy);

void CItemInfoPanel::DestroyBody() {
    vtable_ = ItemInfoPanelVtable();
    inputVtable_ = ItemInfoPanelInputVtable();
    StringArrayDestruct(&skillTextsVtable_);
    StringArrayDestruct(&itemTextsVtable_);
    PartyRecordsDestructOriginal(itemList_);
    DestroyBase();
}
EI_REPLACE_METHOD(0x005EE380, CItemInfoPanel::DestroyBody);

void CChatWindow::DestroyBody() {
    texts_.Destruct();
    for (CRefObject* player : players_)
        if (player) player->Release();
    players_.FreeStorage();
    DestroyBase();
}
EI_REPLACE_METHOD(0x00608ED0, CChatWindow::DestroyBody);

void CUiItemStrip::DestroyForwarding() {
    list_.vtable = ItemArrayVtable();
    list_.FreeData();
    window.DestroyBase();
}
EI_REPLACE_METHOD(0x0060E2B0, CUiItemStrip::DestroyForwarding);

void CSpellMakerPage::DestroyBody() {
    StringArrayDestruct(&textsVtable_);
    DestroyBase();
}
EI_REPLACE_METHOD(0x0060E600, CSpellMakerPage::DestroyBody);

void CHeroStatsPanel::DestroyBody() {
    StringArrayDestruct(&textsVtable_);
    CStringDestroy(&title_);
    DestroyBase();
}
EI_REPLACE_METHOD(0x0060E360, CHeroStatsPanel::DestroyBody);

void CMessageBox::DestroyBody() {
    CStringDestroy(&text_);
    CStringDestroy(&title_);
    DestroyContainer();
}
EI_REPLACE_METHOD(0x0061E3E0, CMessageBox::DestroyBody);
EI_REPLACE_METHOD(0x00633750, CMessageBox::DestroyBody);   // the difficulty question's copy

void CMasterRequestBox::DestroyBody() {
    MasterLinkDestructAt(&link_);
    CMessageBox::DestroyBody();
}
EI_REPLACE_METHOD(0x0061E460, CMasterRequestBox::DestroyBody);

void ChatLine::Destroy() {
    if (CUiSurface* shown = surface) {
        shown->ReleaseContents();
        gamecrt::operator_delete(shown);
    }
    surface = nullptr;
    name.~CString();
    text.~CString();
}
EI_REPLACE_METHOD(0x005E2670, ChatLine::Destroy);

void CChatOverlay::DestroyBody() {
    typed_.~CString();
    DestroyList(lines_, [](ChatLine* line) { line->Destroy(); });
    DestroyBase();
}
EI_REPLACE_METHOD(0x005E28E0, CChatOverlay::DestroyBody);

void CBattleScreenSetup::DestroyBody() {
    FreeVectorStorage(partyWithout_.begin, partyWithout_.capacity);
    FreeVectorStorage(partyWithUnit_.begin, partyWithUnit_.capacity);
    DestroyPersonTexts(personTexts_, personTextCount_);
    heroes_.vtable = DwordArrayVtable();
    heroes_.FreeData();
    Party().vtable = PartyArrayVtable();
    Party().FreeData();
    DestroyContainer();
}
EI_REPLACE_METHOD(0x005F6130, CBattleScreenSetup::DestroyBody);

static_assert(sizeof(BriefingMover) == 0x30);

void CBriefingScreen::DestroyBody() {
    for (BriefingSpeaker* speaker = speakers_; speaker != speakersEnd_; ++speaker) speaker->name.~CString();
    FreeVectorStorage(speakers_, speakersCapacity_);
    people_.FreeStorage();
    // The shown items' list (node 0x18 bytes: next, prev, then the item record).
    for (BriefingShownItem* node = shown_->next; node != shown_;) {
        BriefingShownItem* next = node->next;
        node->name.~CString();
        stlp::deallocate(node, sizeof(BriefingShownItem));
        node = next;
    }
    shown_->next = shown_;
    shown_->prev = shown_;
    stlp::deallocate(shown_, sizeof(BriefingShownItem));
    DestroyPersonTexts(personTexts_, personTextCount_);
    dialogueText_.~CString();
    for (BriefingMover* node = movers_->next; node != movers_;) {
        BriefingMover* next = node->next;
        stlp::deallocate(node, sizeof(BriefingMover));
        node = next;
    }
    movers_->next = movers_;
    movers_->prev = movers_;
    stlp::deallocate(movers_, sizeof(BriefingMover));
    party_.vtable = PartyArrayVtable();
    party_.FreeData();
    topic_.~CString();
    topics_.Destruct();
    DestroyContainer();
}
EI_REPLACE_METHOD(0x005F6BA0, CBriefingScreen::DestroyBody);

void CMpConnectScreen::DestroyBody() {
    texts_.Destruct();
    allodNames_.Destruct();
    hostPassword_.~CString();
    for (MpAddressEntry& entry : entries_) {
        entry.description.~CString();
        entry.name.~CString();
    }
    entries_.FreeStorage();
    DestroyContainer();
}
EI_REPLACE_METHOD(0x00618570, CMpConnectScreen::DestroyBody);

void CHelpWindow::DestroyBody() {
    DestroyList(listB_, [](std::uint32_t*) {});
    DestroyList(pulsing_, [](CUiMesh**) {});
    lines_.Destruct();
    title_.~CString();
    text_.~CString();
    DestroyContainer();
}
EI_REPLACE_METHOD(0x005F8970, CHelpWindow::DestroyBody);

void CObjectivesWindow::DestroyBody() {
    StringArrayDestruct(objectiveTexts_);
    FreeVectorStorage(questPlacesBegin_, questPlacesCapacity_);
    StringArrayDestruct(quests_);
    exitsVtable_ = ObjectiveExitsVtable();
    if (exits_) gamecrt::operator_delete(exits_);
    CStringDestroy(&shownZone_);
    CStringDestroy(&zoneName_);
    for (char** zone = zonesBegin_; zone != zonesEnd_; ++zone) CStringDestroy(zone);
    FreeVectorStorage(zonesBegin_, zonesCapacity_);
    DestroyContainer();
}
EI_REPLACE_METHOD(0x00603B90, CObjectivesWindow::DestroyBody);

void CSaveLoadWindow::DestroyBody() {
    CStringDestroy(&name_);
    for (SaveEntry* entry = entriesBegin_; entry != entriesEnd_; ++entry) {
        entry->stringArrays_[1].Destruct();
        entry->stringArrays_[0].Destruct();
        CStringDestroy(&entry->zone_);
        CStringDestroy(&entry->map_);
        CStringDestroy(&entry->folder_);
        CStringDestroy(&entry->title_);
    }
    FreeVectorStorage(entriesBegin_, entriesCapacity_);
    DestroyContainer();
}
EI_REPLACE_METHOD(0x006070C0, CSaveLoadWindow::DestroyBody);

// Bodies that only jump to the window's destructor body (005FE930): the item screens' icon window
// (00740444), the description panel's base, the camp's panels (item strip, hero equipment and
// quick strip, stats column, constructor pages, repair grid), the figure strips, the game clock and
// the movement panel.
EI_REPLACE_METHOD(0x005EE040, CUiWindow::DestroyBase);
EI_REPLACE_METHOD(0x005EE410, CUiWindow::DestroyBase);
EI_REPLACE_METHOD(0x0060E280, CUiWindow::DestroyBase);
EI_REPLACE_METHOD(0x0060E300, CUiWindow::DestroyBase);
EI_REPLACE_METHOD(0x0060E330, CUiWindow::DestroyBase);
EI_REPLACE_METHOD(0x0060E5D0, CUiWindow::DestroyBase);
EI_REPLACE_METHOD(0x0060E670, CUiWindow::DestroyBase);
EI_REPLACE_METHOD(0x0060E6A0, CUiWindow::DestroyBase);
EI_REPLACE_METHOD(0x006157F0, CUiWindow::DestroyBase);
EI_REPLACE_METHOD(0x00635DA0, CUiWindow::DestroyBase);
EI_REPLACE_METHOD(0x00635DD0, CUiWindow::DestroyBase);
// ... and to the container's (00600BA0): screen 7, the main menu, the escape menu.
EI_REPLACE_METHOD(0x005F5F50, CUiContainer::DestroyContainer);
EI_REPLACE_METHOD(0x005F5F80, CUiContainer::DestroyContainer);
EI_REPLACE_METHOD(0x00603CF0, CUiContainer::DestroyContainer);

// --- Deleting destructors (slot 0) --------------------------------------------------------------

// 005FE910 (the plain window, 00740F60) and the windows whose body is the window's.
CUiWindow* __fastcall UiWindowDelete(CUiWindow* self, void*, unsigned flags) {
    self->DestroyBase();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x005FE910, UiWindowDelete);
EI_REPLACE(0x005EE020, UiWindowDelete);   // the item screens' icon window (00740444)
EI_REPLACE(0x0060E240, UiWindowDelete);   // 00741B58
EI_REPLACE(0x0060E260, UiWindowDelete);   // the item strip (00741A1C)
EI_REPLACE(0x0060E2E0, UiWindowDelete);   // the hero equipment (007418E0)
EI_REPLACE(0x0060E310, UiWindowDelete);   // the quick item strip (00741844)
EI_REPLACE(0x0060E5B0, UiWindowDelete);   // the constructors' first pages (0074170C)
EI_REPLACE(0x0060E650, UiWindowDelete);   // the item maker (007415D4)
EI_REPLACE(0x0060E680, UiWindowDelete);   // the repair grid (00741538)
EI_REPLACE(0x006157D0, UiWindowDelete);   // the figure strips (00741BF4)
EI_REPLACE(0x00635D80, UiWindowDelete);   // the game clock (007425C8)
EI_REPLACE(0x00635DB0, UiWindowDelete);   // the movement panel (00742530)

// 00600B80 (the plain container, 00741008) and the containers whose body is the container's.
CUiContainer* __fastcall UiContainerDelete(CUiContainer* self, void*, unsigned flags) {
    self->DestroyContainer();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x00600B80, UiContainerDelete);
EI_REPLACE(0x005F5F30, UiContainerDelete);   // screen 7 (00740840)
EI_REPLACE(0x005F5F60, UiContainerDelete);   // the main menu (00740688)
EI_REPLACE(0x00603CD0, UiContainerDelete);   // the escape menu (007412A4)

// 005EACD0 / 00602B80: the battle camera's input side (00740208), the inert extra side (00741154).
UiInputSide* __fastcall InputSideDelete(UiInputSide* self, void*, unsigned flags) {
    InputSideDestroy(self);
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x005EACD0, InputSideDelete);
EI_REPLACE(0x00602B80, InputSideDelete);

// 005E28C0: the chat overlay (007400DC).
CChatOverlay* __fastcall ChatOverlayDelete(CChatOverlay* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x005E28C0, ChatOverlayDelete);

// 005EE420: the description panel (007404DC).
CItemInfoPanel* __fastcall ItemInfoPanelDelete(CItemInfoPanel* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x005EE420, ItemInfoPanelDelete);

// 005F5FC0: the in-game screen (00740764).
CBattleScreenSetup* __fastcall BattleScreenSetupDelete(CBattleScreenSetup* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x005F5FC0, BattleScreenSetupDelete);

// 005F6450 / 005F6600 / 005F6FF0: the item screen (00740A2C), the camp (0074091C), screen 9
// (00740D18).
CItemScreen* __fastcall ItemScreenDelete(CItemScreen* self, void*, unsigned flags) {
    ItemScreenDestroy(self);
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x005F6450, ItemScreenDelete);
CItemScreen* __fastcall CampScreenDelete(CItemScreen* self, void*, unsigned flags) {
    CampScreenDestroy(self);
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x005F6600, CampScreenDelete);
CItemScreen* __fastcall Screen9Delete(CItemScreen* self, void*, unsigned flags) {
    Screen9Destroy(self);
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x005F6FF0, Screen9Delete);

// 005F6900: the global map (00740B3C).
CGlobalMap* __fastcall GlobalMapDelete(CGlobalMap* self, void*, unsigned flags) {
    GlobalMapDestroy(self);
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x005F6900, GlobalMapDelete);

// 005F6B80: the briefing / base screen (007405AC).
CBriefingScreen* __fastcall BriefingDelete(CBriefingScreen* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x005F6B80, BriefingDelete);

// 005F6E20: screen 8, the titles (00740C3C).
void* __fastcall Screen8Delete(void* self, void*, unsigned flags) {
    Screen8Destroy(self);
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x005F6E20, Screen8Delete);

// 005F8950: the help window (00740E28).
CHelpWindow* __fastcall HelpWindowDelete(CHelpWindow* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x005F8950, HelpWindowDelete);

// 00603B70: the objectives window (007411BC).
CObjectivesWindow* __fastcall ObjectivesDelete(CObjectivesWindow* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x00603B70, ObjectivesDelete);

// 006070A0: the save / load window (00741388).
CSaveLoadWindow* __fastcall SaveLoadDelete(CSaveLoadWindow* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x006070A0, SaveLoadDelete);

// 00608EB0: the chat window (00741478).
CChatWindow* __fastcall ChatWindowDelete(CChatWindow* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x00608EB0, ChatWindowDelete);

// 0060E290: the forwarding item strip (0074197C).
CUiItemStrip* __fastcall ForwardingStripDelete(CUiItemStrip* self, void*, unsigned flags) {
    self->DestroyForwarding();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0060E290, ForwardingStripDelete);

// 0060E340: the statistics column (00741ABC).
CHeroStatsPanel* __fastcall StatsPanelDelete(CHeroStatsPanel* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0060E340, StatsPanelDelete);

// 0060E3C0: the trade panel (007417A8).
CUiWindow* __fastcall TradePanelDelete(CUiWindow* self, void*, unsigned flags) {
    TradePanelDestroy(self);
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0060E3C0, TradePanelDelete);

// 0060E5E0: the spell maker (00741670).
CSpellMakerPage* __fastcall SpellMakerDelete(CSpellMakerPage* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0060E5E0, SpellMakerDelete);

// 00618550: the multiplayer connection screen (00741CD0).
CMpConnectScreen* __fastcall ConnectScreenDelete(CMpConnectScreen* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x00618550, ConnectScreenDelete);

// 0061E440: the master request box (00741DD8).
CMasterRequestBox* __fastcall MasterRequestBoxDelete(CMasterRequestBox* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0061E440, MasterRequestBoxDelete);

// 0062B160: the options window (00741FAC).
COptionsWindow* __fastcall OptionsWindowDelete(COptionsWindow* self, void*, unsigned flags) {
    self->DestroyOptions();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0062B160, OptionsWindowDelete);

// 0062EDA0: the global map's clouds (slot 2 of 007420D4): the base's own (00499E30) does the same.
EI_REPLACE(0x0062EDA0, CloudNoiseBaseDeletingDestruct);

// 00633240: the multiplayer hero screen (0074214C).
CMpHeroScreen* __fastcall HeroScreenDelete(CMpHeroScreen* self, void*, unsigned flags) {
    self->Destroy();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x00633240, HeroScreenDelete);

// 00633730 / 00634770: the difficulty question (0074225C), the message box (00742348).
CMessageBox* __fastcall MessageBoxDelete(CMessageBox* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x00633730, MessageBoxDelete);
EI_REPLACE(0x00634770, MessageBoxDelete);

// The battle HUD's panels (bodies in ui_hud_destroy.cpp and ui_screen_constructors.cpp).
CHudMap* __fastcall HudMapDelete(CHudMap* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x00635C80, HudMapDelete);
CHudUnitPanel* __fastcall HudUnitPanelDelete(CHudUnitPanel* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x00635CE0, HudUnitPanelDelete);
CHudTopPanel* __fastcall HudTopPanelDelete(CHudTopPanel* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x00635DE0, HudTopPanelDelete);
CHudWeaponBar* __fastcall HudWeaponBarDelete(CHudWeaponBar* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0063BC50, HudWeaponBarDelete);
CHudQuickItems* __fastcall HudQuickItemsDelete(CHudQuickItems* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0063CA50, HudQuickItemsDelete);
CHudSpellColumn* __fastcall HudSpellColumnDelete(CHudSpellColumn* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0063E440, HudSpellColumnDelete);
CHudActionColumn* __fastcall HudActionColumnDelete(CHudActionColumn* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0063F470, HudActionColumnDelete);
CHudPortraits* __fastcall HudPortraitsDelete(CHudPortraits* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0063FF50, HudPortraitsDelete);

// --- The input sides' slot 0 (SUB ECX,0x94; JMP to the window's) -------------------------------

void* __fastcall ChatOverlayInputDelete(void* input, void*, unsigned flags) {
    return ChatOverlayDelete(WindowFromInput<CChatOverlay>(input), nullptr, flags);
}
EI_REPLACE(0x005E3C80, ChatOverlayInputDelete);
void* __fastcall UiWindowInputDelete(void* input, void*, unsigned flags) {
    return UiWindowDelete(WindowFromInput<CUiWindow>(input), nullptr, flags);
}
EI_REPLACE(0x00600AC0, UiWindowInputDelete);
EI_REPLACE(0x005F45F0, UiWindowInputDelete);
EI_REPLACE(0x00613D50, UiWindowInputDelete);
EI_REPLACE(0x00613D60, UiWindowInputDelete);
EI_REPLACE(0x00613D70, UiWindowInputDelete);
EI_REPLACE(0x00613D80, UiWindowInputDelete);
EI_REPLACE(0x00613D90, UiWindowInputDelete);
EI_REPLACE(0x00613DA0, UiWindowInputDelete);
void* __fastcall StatsPanelInputDelete(void* input, void*, unsigned flags) {
    return StatsPanelDelete(WindowFromInput<CHeroStatsPanel>(input), nullptr, flags);
}
EI_REPLACE(0x00613DD0, StatsPanelInputDelete);
EI_REPLACE(0x00613DE0, UiWindowInputDelete);
EI_REPLACE(0x0061B210, UiWindowInputDelete);
void* __fastcall UiContainerInputDelete(void* input, void*, unsigned flags) {
    return UiContainerDelete(WindowFromInput<CUiContainer>(input), nullptr, flags);
}
EI_REPLACE(0x00601E70, UiContainerInputDelete);
EI_REPLACE(0x005FC140, UiContainerInputDelete);
EI_REPLACE(0x005FC120, UiContainerInputDelete);
EI_REPLACE(0x0060C7E0, UiContainerInputDelete);
void* __fastcall ItemInfoPanelInputDelete(void* input, void*, unsigned flags) {
    return ItemInfoPanelDelete(WindowFromInput<CItemInfoPanel>(input), nullptr, flags);
}
EI_REPLACE(0x005F4600, ItemInfoPanelInputDelete);
void* __fastcall BattleScreenSetupInputDelete(void* input, void*, unsigned flags) {
    return BattleScreenSetupDelete(WindowFromInput<CBattleScreenSetup>(input), nullptr, flags);
}
EI_REPLACE(0x005FC0E0, BattleScreenSetupInputDelete);
void* __fastcall ItemScreenInputDelete(void* input, void*, unsigned flags) {
    return ItemScreenDelete(WindowFromInput<CItemScreen>(input), nullptr, flags);
}
EI_REPLACE(0x005FC0C0, ItemScreenInputDelete);
void* __fastcall CampScreenInputDelete(void* input, void*, unsigned flags) {
    return CampScreenDelete(WindowFromInput<CItemScreen>(input), nullptr, flags);
}
EI_REPLACE(0x005FC100, CampScreenInputDelete);
void* __fastcall Screen9InputDelete(void* input, void*, unsigned flags) {
    return Screen9Delete(WindowFromInput<CItemScreen>(input), nullptr, flags);
}
EI_REPLACE(0x005FC110, Screen9InputDelete);
void* __fastcall GlobalMapInputDelete(void* input, void*, unsigned flags) {
    return GlobalMapDelete(WindowFromInput<CGlobalMap>(input), nullptr, flags);
}
EI_REPLACE(0x005FC130, GlobalMapInputDelete);
void* __fastcall BriefingInputDelete(void* input, void*, unsigned flags) {
    return BriefingDelete(WindowFromInput<CBriefingScreen>(input), nullptr, flags);
}
EI_REPLACE(0x005FC0B0, BriefingInputDelete);
void* __fastcall Screen8InputDelete(void* input, void*, unsigned flags) {
    return Screen8Delete(WindowFromInput<void>(input), nullptr, flags);
}
EI_REPLACE(0x005FC0F0, Screen8InputDelete);
void* __fastcall HelpWindowInputDelete(void* input, void*, unsigned flags) {
    return HelpWindowDelete(WindowFromInput<CHelpWindow>(input), nullptr, flags);
}
EI_REPLACE(0x005FC0D0, HelpWindowInputDelete);
void* __fastcall ObjectivesInputDelete(void* input, void*, unsigned flags) {
    return ObjectivesDelete(WindowFromInput<CObjectivesWindow>(input), nullptr, flags);
}
EI_REPLACE(0x0060C7C0, ObjectivesInputDelete);
void* __fastcall SaveLoadInputDelete(void* input, void*, unsigned flags) {
    return SaveLoadDelete(WindowFromInput<CSaveLoadWindow>(input), nullptr, flags);
}
EI_REPLACE(0x0060C7B0, SaveLoadInputDelete);
void* __fastcall ChatWindowInputDelete(void* input, void*, unsigned flags) {
    return ChatWindowDelete(WindowFromInput<CChatWindow>(input), nullptr, flags);
}
EI_REPLACE(0x0060C7D0, ChatWindowInputDelete);
void* __fastcall ForwardingStripInputDelete(void* input, void*, unsigned flags) {
    return ForwardingStripDelete(WindowFromInput<CUiItemStrip>(input), nullptr, flags);
}
EI_REPLACE(0x00613DC0, ForwardingStripInputDelete);
void* __fastcall TradePanelInputDelete(void* input, void*, unsigned flags) {
    return TradePanelDelete(WindowFromInput<CUiWindow>(input), nullptr, flags);
}
EI_REPLACE(0x00613DB0, TradePanelInputDelete);
void* __fastcall SpellMakerInputDelete(void* input, void*, unsigned flags) {
    return SpellMakerDelete(WindowFromInput<CSpellMakerPage>(input), nullptr, flags);
}
EI_REPLACE(0x00613DF0, SpellMakerInputDelete);
void* __fastcall ConnectScreenInputDelete(void* input, void*, unsigned flags) {
    return ConnectScreenDelete(WindowFromInput<CMpConnectScreen>(input), nullptr, flags);
}
EI_REPLACE(0x0061B220, ConnectScreenInputDelete);
void* __fastcall MasterRequestBoxInputDelete(void* input, void*, unsigned flags) {
    return MasterRequestBoxDelete(WindowFromInput<CMasterRequestBox>(input), nullptr, flags);
}
EI_REPLACE(0x00620300, MasterRequestBoxInputDelete);
void* __fastcall OptionsWindowInputDelete(void* input, void*, unsigned flags) {
    return OptionsWindowDelete(WindowFromInput<COptionsWindow>(input), nullptr, flags);
}
EI_REPLACE(0x0062B770, OptionsWindowInputDelete);
void* __fastcall HeroScreenInputDelete(void* input, void*, unsigned flags) {
    return HeroScreenDelete(WindowFromInput<CMpHeroScreen>(input), nullptr, flags);
}
EI_REPLACE(0x006345F0, HeroScreenInputDelete);
void* __fastcall MessageBoxInputDelete(void* input, void*, unsigned flags) {
    return MessageBoxDelete(WindowFromInput<CMessageBox>(input), nullptr, flags);
}
EI_REPLACE(0x006345E0, MessageBoxInputDelete);
EI_REPLACE(0x00634FD0, MessageBoxInputDelete);

// The battle HUD panels' input sides (0064BCF0-0064BD80).
#define EI_HUD_INPUT_DELETE(Name, Cls, Delete)                         \
    void* __fastcall Name(void* input, void*, unsigned flags) {         \
        return Delete(WindowFromInput<Cls>(input), nullptr, flags);     \
    }
EI_HUD_INPUT_DELETE(HudWeaponBarInputDelete, CHudWeaponBar, HudWeaponBarDelete)
EI_HUD_INPUT_DELETE(HudQuickItemsInputDelete, CHudQuickItems, HudQuickItemsDelete)
EI_HUD_INPUT_DELETE(HudSpellColumnInputDelete, CHudSpellColumn, HudSpellColumnDelete)
EI_HUD_INPUT_DELETE(HudActionColumnInputDelete, CHudActionColumn, HudActionColumnDelete)
EI_HUD_INPUT_DELETE(HudPortraitsInputDelete, CHudPortraits, HudPortraitsDelete)
EI_HUD_INPUT_DELETE(HudUnitPanelInputDelete, CHudUnitPanel, HudUnitPanelDelete)
EI_HUD_INPUT_DELETE(HudMapInputDelete, CHudMap, HudMapDelete)
EI_HUD_INPUT_DELETE(HudTopPanelInputDelete, CHudTopPanel, HudTopPanelDelete)
#undef EI_HUD_INPUT_DELETE
EI_REPLACE(0x0064BCF0, HudWeaponBarInputDelete);
EI_REPLACE(0x0064BD00, HudQuickItemsInputDelete);
EI_REPLACE(0x0064BD10, HudSpellColumnInputDelete);
EI_REPLACE(0x0064BD20, HudActionColumnInputDelete);
EI_REPLACE(0x0064BD30, HudPortraitsInputDelete);
EI_REPLACE(0x0064BD40, HudUnitPanelInputDelete);
EI_REPLACE(0x0064BD50, HudMapInputDelete);
EI_REPLACE(0x0064BD60, UiWindowInputDelete);   // the game clock
EI_REPLACE(0x0064BD70, UiWindowInputDelete);   // the movement panel
EI_REPLACE(0x0064BD80, HudTopPanelInputDelete);
