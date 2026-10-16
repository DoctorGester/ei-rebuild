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
void DestroyPersonTexts(PersonTextMap& map) {
    PersonTextNode* header = map.end();
    if (map.size() != 0) {
        PersonTextsEraseAt(&map, nullptr, header->parent);
        map.ResetToEmpty();
    }
    stlp::deallocate(map.end(), sizeof(PersonTextNode));
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

// 005EA850 (RET 4): the base input side's own deleting destructor (slot 0 of 007401D0): its
// vtable stored, freed with bit 0 of `flags`; returns the side.
UiInputSide* UiInputSide::InputSideBaseDelete(unsigned flags) {
    vtable_ = InputSideBaseVtable();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005EA850, UiInputSide::InputSideBaseDelete);

void CItemInfoPanel::DestroyBody() {
    vtable_ = ItemInfoPanelVtable();
    inputVtable_ = ItemInfoPanelInputVtable();
    StringArrayDestruct(&skillTexts_);
    StringArrayDestruct(&itemTexts_);
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
    StringArrayDestruct(&texts_);
    DestroyBase();
}
EI_REPLACE_METHOD(0x0060E600, CSpellMakerPage::DestroyBody);

void CHeroStatsPanel::DestroyBody() {
    StringArrayDestruct(&texts_);
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
    net::MasterLinkDestruct(&link_);   // 00440D40
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

void CBattleScreen::DestroyBody() {
    FreeVectorStorage(clientParty_.without.begin(), clientParty_.without.EndOfStorage());
    FreeVectorStorage(clientParty_.withUnit.begin(), clientParty_.withUnit.EndOfStorage());
    DestroyPersonTexts(personTexts_);
    heroes_.vtable = DwordArrayVtable();
    heroes_.FreeData();
    party_.vtable = PartyArrayVtable();
    party_.FreeData();
    DestroyContainer();
}
EI_REPLACE_METHOD(0x005F6130, CBattleScreen::DestroyBody);

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
    DestroyPersonTexts(personTexts_);
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
    DestroyList(pictures_, [](CUiMesh**) {});
    DestroyList(pulsing_, [](CUiMesh**) {});
    lines_.Destruct();
    title_.~CString();
    text_.~CString();
    DestroyContainer();
}
EI_REPLACE_METHOD(0x005F8970, CHelpWindow::DestroyBody);

void CObjectivesWindow::DestroyBody() {
    StringArrayDestruct(&objectiveTexts_);
    FreeVectorStorage(questPlaces_.begin(), questPlaces_.EndOfStorage());
    StringArrayDestruct(&quests_);
    exits_.vtable = ObjectiveExitsVtable();
    if (exits_.data) gamecrt::operator_delete(exits_.data);
    CStringDestroy(&shownZone_);
    CStringDestroy(&zoneName_);
    for (mfc::CString* zone = zones_.begin(); zone != zones_.end(); ++zone) CStringDestroy(zone);
    FreeVectorStorage(zones_.begin(), zones_.EndOfStorage());
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
CUiWindow* CUiWindow::UiWindowDelete(unsigned flags) {
    DestroyBase();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x005FE910, CUiWindow::UiWindowDelete);
EI_REPLACE_METHOD(0x005EE020, CUiWindow::UiWindowDelete);   // the item screens' icon window (00740444)
EI_REPLACE_METHOD(0x0060E240, CUiWindow::UiWindowDelete);   // 00741B58
EI_REPLACE_METHOD(0x0060E260, CUiWindow::UiWindowDelete);   // the item strip (00741A1C)
EI_REPLACE_METHOD(0x0060E2E0, CUiWindow::UiWindowDelete);   // the hero equipment (007418E0)
EI_REPLACE_METHOD(0x0060E310, CUiWindow::UiWindowDelete);   // the quick item strip (00741844)
EI_REPLACE_METHOD(0x0060E5B0, CUiWindow::UiWindowDelete);   // the constructors' first pages (0074170C)
EI_REPLACE_METHOD(0x0060E650, CUiWindow::UiWindowDelete);   // the item maker (007415D4)
EI_REPLACE_METHOD(0x0060E680, CUiWindow::UiWindowDelete);   // the repair grid (00741538)
EI_REPLACE_METHOD(0x006157D0, CUiWindow::UiWindowDelete);   // the figure strips (00741BF4)
EI_REPLACE_METHOD(0x00635D80, CUiWindow::UiWindowDelete);   // the game clock (007425C8)
EI_REPLACE_METHOD(0x00635DB0, CUiWindow::UiWindowDelete);   // the movement panel (00742530)

// 00600B80 (the plain container, 00741008) and the containers whose body is the container's.
CUiContainer* CUiContainer::UiContainerDelete(unsigned flags) {
    DestroyContainer();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x00600B80, CUiContainer::UiContainerDelete);
EI_REPLACE_METHOD(0x005F5F30, CUiContainer::UiContainerDelete);   // screen 7 (00740840)
EI_REPLACE_METHOD(0x005F5F60, CUiContainer::UiContainerDelete);   // the main menu (00740688)
EI_REPLACE_METHOD(0x00603CD0, CUiContainer::UiContainerDelete);   // the escape menu (007412A4)

// 005EACD0 / 00602B80: the battle camera's input side (00740208), the inert extra side (00741154).
UiInputSide* UiInputSide::InputSideDelete(unsigned flags) {
    InputSideDestroy(this);
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x005EACD0, UiInputSide::InputSideDelete);
EI_REPLACE_METHOD(0x00602B80, UiInputSide::InputSideDelete);

// 005E28C0: the chat overlay (007400DC).
CChatOverlay* CChatOverlay::ChatOverlayDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x005E28C0, CChatOverlay::ChatOverlayDelete);

// 005EE420: the description panel (007404DC).
CItemInfoPanel* CItemInfoPanel::ItemInfoPanelDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x005EE420, CItemInfoPanel::ItemInfoPanelDelete);

// 005F5FC0: the in-game screen (00740764).
CBattleScreen* CBattleScreen::BattleScreenDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x005F5FC0, CBattleScreen::BattleScreenDelete);

// 005F6450 / 005F6600 / 005F6FF0: the item screen (00740A2C), the camp (0074091C), screen 9
// (00740D18).
CItemScreen* CItemScreen::ItemScreenDelete(unsigned flags) {
    ItemScreenDestroy(this);
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x005F6450, CItemScreen::ItemScreenDelete);
CItemScreen* CItemScreen::CampScreenDelete(unsigned flags) {
    CampScreenDestroy(this);
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x005F6600, CItemScreen::CampScreenDelete);
CItemScreen* CItemScreen::Screen9Delete(unsigned flags) {
    Screen9Destroy(this);
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x005F6FF0, CItemScreen::Screen9Delete);

// 005F6900: the global map (00740B3C).
CGlobalMap* CGlobalMap::GlobalMapDelete(unsigned flags) {
    GlobalMapDestroy(this);
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x005F6900, CGlobalMap::GlobalMapDelete);

// 005F6B80: the briefing / base screen (007405AC).
CBriefingScreen* CBriefingScreen::BriefingDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x005F6B80, CBriefingScreen::BriefingDelete);

// 005F6E20: screen 8, the titles (00740C3C).
CTitlesScreen* CTitlesScreen::TitlesDelete(unsigned flags) {
    Screen8Destroy(this);
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x005F6E20, CTitlesScreen::TitlesDelete);

// 005F8950: the help window (00740E28).
CHelpWindow* CHelpWindow::HelpWindowDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x005F8950, CHelpWindow::HelpWindowDelete);

// 00603B70: the objectives window (007411BC).
CObjectivesWindow* CObjectivesWindow::ObjectivesDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x00603B70, CObjectivesWindow::ObjectivesDelete);

// 006070A0: the save / load window (00741388).
CSaveLoadWindow* CSaveLoadWindow::SaveLoadDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x006070A0, CSaveLoadWindow::SaveLoadDelete);

// 00608EB0: the chat window (00741478).
CChatWindow* CChatWindow::ChatWindowDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x00608EB0, CChatWindow::ChatWindowDelete);

// 0060E290: the forwarding item strip (0074197C).
CUiItemStrip* CUiItemStrip::ForwardingStripDelete(unsigned flags) {
    DestroyForwarding();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0060E290, CUiItemStrip::ForwardingStripDelete);

// 0060E340: the statistics column (00741ABC).
CHeroStatsPanel* CHeroStatsPanel::StatsPanelDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0060E340, CHeroStatsPanel::StatsPanelDelete);

// 0060E3C0: the trade panel (007417A8).
CUiWindow* CUiWindow::TradePanelDelete(unsigned flags) {
    TradePanelDestroy(this);
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0060E3C0, CUiWindow::TradePanelDelete);

// 0060E5E0: the spell maker (00741670).
CSpellMakerPage* CSpellMakerPage::SpellMakerDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0060E5E0, CSpellMakerPage::SpellMakerDelete);

// 00618550: the multiplayer connection screen (00741CD0).
CMpConnectScreen* CMpConnectScreen::ConnectScreenDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x00618550, CMpConnectScreen::ConnectScreenDelete);

// 0061E440: the master request box (00741DD8).
CMasterRequestBox* CMasterRequestBox::MasterRequestBoxDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0061E440, CMasterRequestBox::MasterRequestBoxDelete);

// 0062B160: the options window (00741FAC).
COptionsWindow* COptionsWindow::OptionsWindowDelete(unsigned flags) {
    DestroyOptions();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0062B160, COptionsWindow::OptionsWindowDelete);

// 0062EDA0: the global map's clouds (slot 2 of 007420D4): the base's own (00499E30) does the same.
EI_REPLACE(0x0062EDA0, CloudNoiseBaseDeletingDestruct);

// 00633240: the multiplayer hero screen (0074214C).
CMpHeroScreen* CMpHeroScreen::HeroScreenDelete(unsigned flags) {
    Destroy();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x00633240, CMpHeroScreen::HeroScreenDelete);

// 00633730 / 00634770: the difficulty question (0074225C), the message box (00742348).
CMessageBox* CMessageBox::MessageBoxDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x00633730, CMessageBox::MessageBoxDelete);
EI_REPLACE_METHOD(0x00634770, CMessageBox::MessageBoxDelete);

// The battle HUD's panels (bodies in ui_hud_destroy.cpp and ui_screen_constructors.cpp).
CHudMap* CHudMap::HudMapDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x00635C80, CHudMap::HudMapDelete);
CHudUnitPanel* CHudUnitPanel::HudUnitPanelDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x00635CE0, CHudUnitPanel::HudUnitPanelDelete);
CHudTopPanel* CHudTopPanel::HudTopPanelDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x00635DE0, CHudTopPanel::HudTopPanelDelete);
CHudWeaponBar* CHudWeaponBar::HudWeaponBarDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0063BC50, CHudWeaponBar::HudWeaponBarDelete);
CHudQuickItems* CHudQuickItems::HudQuickItemsDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0063CA50, CHudQuickItems::HudQuickItemsDelete);
CHudSpellColumn* CHudSpellColumn::HudSpellColumnDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0063E440, CHudSpellColumn::HudSpellColumnDelete);
CHudActionColumn* CHudActionColumn::HudActionColumnDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0063F470, CHudActionColumn::HudActionColumnDelete);
CHudPortraits* CHudPortraits::HudPortraitsDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0063FF50, CHudPortraits::HudPortraitsDelete);

// --- The input sides' slot 0 (SUB ECX,0x94; JMP to the window's) -------------------------------

// Each is UiInput::DeleteWindow of the window class's own deleting destructor.
EI_REPLACE_METHOD(0x005E3C80, UiInput::DeleteWindow<&CChatOverlay::ChatOverlayDelete>);
EI_REPLACE_METHOD(0x00600AC0, UiInput::DeleteWindow<&CUiWindow::UiWindowDelete>);
EI_REPLACE_METHOD(0x005F45F0, UiInput::DeleteWindow<&CUiWindow::UiWindowDelete>);
EI_REPLACE_METHOD(0x00613D50, UiInput::DeleteWindow<&CUiWindow::UiWindowDelete>);
EI_REPLACE_METHOD(0x00613D60, UiInput::DeleteWindow<&CUiWindow::UiWindowDelete>);
EI_REPLACE_METHOD(0x00613D70, UiInput::DeleteWindow<&CUiWindow::UiWindowDelete>);
EI_REPLACE_METHOD(0x00613D80, UiInput::DeleteWindow<&CUiWindow::UiWindowDelete>);
EI_REPLACE_METHOD(0x00613D90, UiInput::DeleteWindow<&CUiWindow::UiWindowDelete>);
EI_REPLACE_METHOD(0x00613DA0, UiInput::DeleteWindow<&CUiWindow::UiWindowDelete>);
EI_REPLACE_METHOD(0x00613DD0, UiInput::DeleteWindow<&CHeroStatsPanel::StatsPanelDelete>);
EI_REPLACE_METHOD(0x00613DE0, UiInput::DeleteWindow<&CUiWindow::UiWindowDelete>);
EI_REPLACE_METHOD(0x0061B210, UiInput::DeleteWindow<&CUiWindow::UiWindowDelete>);
EI_REPLACE_METHOD(0x00601E70, UiInput::DeleteWindow<&CUiContainer::UiContainerDelete>);
EI_REPLACE_METHOD(0x005FC140, UiInput::DeleteWindow<&CUiContainer::UiContainerDelete>);
EI_REPLACE_METHOD(0x005FC120, UiInput::DeleteWindow<&CUiContainer::UiContainerDelete>);
EI_REPLACE_METHOD(0x0060C7E0, UiInput::DeleteWindow<&CUiContainer::UiContainerDelete>);
EI_REPLACE_METHOD(0x005F4600, UiInput::DeleteWindow<&CItemInfoPanel::ItemInfoPanelDelete>);
EI_REPLACE_METHOD(0x005FC0E0, UiInput::DeleteWindow<&CBattleScreen::BattleScreenDelete>);
EI_REPLACE_METHOD(0x005FC0C0, UiInput::DeleteWindow<&CItemScreen::ItemScreenDelete>);
EI_REPLACE_METHOD(0x005FC100, UiInput::DeleteWindow<&CItemScreen::CampScreenDelete>);
EI_REPLACE_METHOD(0x005FC110, UiInput::DeleteWindow<&CItemScreen::Screen9Delete>);
EI_REPLACE_METHOD(0x005FC130, UiInput::DeleteWindow<&CGlobalMap::GlobalMapDelete>);
EI_REPLACE_METHOD(0x005FC0B0, UiInput::DeleteWindow<&CBriefingScreen::BriefingDelete>);
EI_REPLACE_METHOD(0x005FC0F0, UiInput::DeleteWindow<&CTitlesScreen::TitlesDelete>);
EI_REPLACE_METHOD(0x005FC0D0, UiInput::DeleteWindow<&CHelpWindow::HelpWindowDelete>);
EI_REPLACE_METHOD(0x0060C7C0, UiInput::DeleteWindow<&CObjectivesWindow::ObjectivesDelete>);
EI_REPLACE_METHOD(0x0060C7B0, UiInput::DeleteWindow<&CSaveLoadWindow::SaveLoadDelete>);
EI_REPLACE_METHOD(0x0060C7D0, UiInput::DeleteWindow<&CChatWindow::ChatWindowDelete>);
EI_REPLACE_METHOD(0x00613DC0, UiInput::DeleteWindow<&CUiItemStrip::ForwardingStripDelete>);
EI_REPLACE_METHOD(0x00613DB0, UiInput::DeleteWindow<&CUiWindow::TradePanelDelete>);
EI_REPLACE_METHOD(0x00613DF0, UiInput::DeleteWindow<&CSpellMakerPage::SpellMakerDelete>);
EI_REPLACE_METHOD(0x0061B220, UiInput::DeleteWindow<&CMpConnectScreen::ConnectScreenDelete>);
EI_REPLACE_METHOD(0x00620300, UiInput::DeleteWindow<&CMasterRequestBox::MasterRequestBoxDelete>);
EI_REPLACE_METHOD(0x0062B770, UiInput::DeleteWindow<&COptionsWindow::OptionsWindowDelete>);
EI_REPLACE_METHOD(0x006345F0, UiInput::DeleteWindow<&CMpHeroScreen::HeroScreenDelete>);
EI_REPLACE_METHOD(0x006345E0, UiInput::DeleteWindow<&CMessageBox::MessageBoxDelete>);
EI_REPLACE_METHOD(0x00634FD0, UiInput::DeleteWindow<&CMessageBox::MessageBoxDelete>);
EI_REPLACE_METHOD(0x0064BCF0, UiInput::DeleteWindow<&CHudWeaponBar::HudWeaponBarDelete>);
EI_REPLACE_METHOD(0x0064BD00, UiInput::DeleteWindow<&CHudQuickItems::HudQuickItemsDelete>);
EI_REPLACE_METHOD(0x0064BD10, UiInput::DeleteWindow<&CHudSpellColumn::HudSpellColumnDelete>);
EI_REPLACE_METHOD(0x0064BD20, UiInput::DeleteWindow<&CHudActionColumn::HudActionColumnDelete>);
EI_REPLACE_METHOD(0x0064BD30, UiInput::DeleteWindow<&CHudPortraits::HudPortraitsDelete>);
EI_REPLACE_METHOD(0x0064BD40, UiInput::DeleteWindow<&CHudUnitPanel::HudUnitPanelDelete>);
EI_REPLACE_METHOD(0x0064BD50, UiInput::DeleteWindow<&CHudMap::HudMapDelete>);
EI_REPLACE_METHOD(0x0064BD60, UiInput::DeleteWindow<&CUiWindow::UiWindowDelete>);   // the game clock
EI_REPLACE_METHOD(0x0064BD70, UiInput::DeleteWindow<&CUiWindow::UiWindowDelete>);   // the movement panel
EI_REPLACE_METHOD(0x0064BD80, UiInput::DeleteWindow<&CHudTopPanel::HudTopPanelDelete>);
