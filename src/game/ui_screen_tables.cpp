// The containers' and screens' compiled tables (C19; ui_container_class.h, docs/notes/standalone.md "Compiler-made
// vtables"): a tag per game.exe table the screens' constructors stored, each with the slots its table has of its own;
// the accessors the constructors and the harness call. game.exe's addresses are in the comments.
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_container_class.h"
#include "ui_create_screen.h"
#include "ui_item_info.h"
#include "ui_screens.h"
#include "ui_view_screen.h"

namespace {

// --- The container (00741008 / 00740FD4) ----------------------------------------------------------

struct ContainerBaseClass final : ContainerClass<ContainerBaseClass, &CUiContainer::UiContainerDelete> {};
struct ContainerBaseInputClass final : ContainerInputClass<ContainerBaseInputClass, &CUiContainer::UiContainerDelete> {};

// --- The battle screen (00740764 / 00740730) -------------------------------------------------------

struct BattleScreenClass final : ContainerClass<BattleScreenClass, &CBattleScreen::BattleScreenDelete> {
    BOOL Draw(int pass) override { return AsScreen()->Draw(reinterpret_cast<void*>(pass)); }   // 20: 00636020
    void Update() override { AsScreen()->Update(); }                                          // 24: 00638280
    void CreateScreen() override { AsScreen()->Create(); }                                    // 25: 00635250
    void CloseScreen() override { AsScreen()->Close(); }                                      // 26: 00635E60
    void MessageResult(int result) override { AsScreen()->OnMessageResult(result); }          // 27: 00635EB0
    void ItemsPicked(void* items, int money, int crippled) override {                         // 28: 00636150
        AsScreen()->OnItemsPicked(static_cast<mfc::CTypedPtrList<CItem>*>(items), money, crippled);
    }
    void UnitJoined(CUnit* unit) override { AsScreen()->OnUnitJoined(unit); }                 // 29: 006361B0
    void UnitLeft(CUnit* unit) override { AsScreen()->OnUnitLeft(unit); }                     // 30: 006364E0
    void GameOver(int won) override { AsScreen()->OnGameOver(static_cast<char>(won)); }       // 31: 006365E0
    void Speech(int id, unsigned event, char* extra) override { AsScreen()->OnUnitSpeech(id, event, extra); }   // 32: 006366F0
    void QuestVariable(const char* name, float value) override { AsScreen()->OnQuestVariable(name, value); }    // 33: 006369D0
    bool ReadyToStopPolling() override { return AsScreen()->PollShown(); }                    // 35: 006360F0
    void PlayerList(int mode) override { AsScreen()->SetPortraitMode(mode); }                 // 38: 005F5FB0
    void SaveState() override { AsScreen()->SaveState(); }                                    // 40: 00635080
};
struct BattleScreenInputClass final : ContainerInputClass<BattleScreenInputClass, &CBattleScreen::BattleScreenDelete> {
    void Deactivate() override { AsScreen()->OnDeactivate(); }                                        // 1: 00638A10
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnLButtonDown(b, x, y), 0; }      // 2: 00638A40
    int LeftUp(int b, int x, int y) override { return AsScreen()->OnLButtonUp(b, x, y), 0; }          // 3: 00638B30
    int MouseMove(int b, int x, int y) override { return AsScreen()->OnMouseMove(b, x, y), 0; }       // 4: 00639D60
    int LeftDouble(int b, int x, int y) override { return AsScreen()->OnLeftDouble(b, x, y), 0; }     // 7: 00638AD0
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }           // 9: 0063A560
    int KeyUp(int k, int r, int f) override { return AsScreen()->OnKeyUp(k, r, f), 0; }               // 11: 0063B6D0
    int Wheel(int f, int d, int x, int y) override { return AsScreen()->OnWheel(f, d, x, y); }        // 12: 0063B770
};

// --- The main menu (00740688 / 00740654) -----------------------------------------------------------

struct MainMenuClass final : ContainerClass<MainMenuClass, &CUiContainer::UiContainerDelete, CMainMenu> {
    BOOL Draw(int pass) override { return AsScreen()->DrawImpl(pass); }                // 20: 00632A90
    void Update() override { AsScreen()->Update(); }                                   // 24: 00632B10
    void CreateScreen() override { AsScreen()->Create(); }                             // 25: 006326B0
    void CloseScreen() override { AsScreen()->Close(); }                               // 26: 00632910
    void MessageResult(int result) override { AsScreen()->OnMessageResult(result); }   // 27: 00632940
};
struct MainMenuInputClass final : ContainerInputClass<MainMenuInputClass, &CUiContainer::UiContainerDelete, CMainMenu> {
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnLButtonDown(b, x, y), 0; }   // 2: 00632D70
    int MouseMove(int b, int x, int y) override { return AsScreen()->OnMouseMove(b, x, y), 0; }    // 4: 00633400
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }        // 9: 00633540
};

// --- The briefing screen (007405AC / 00740578) -----------------------------------------------------

struct BriefingScreenClass final : ContainerClass<BriefingScreenClass, &CBriefingScreen::BriefingDelete> {
    BOOL Draw(int pass) override { return AsScreen()->DrawImpl(pass); }                // 20: 00602BF0
    void Update() override { AsScreen()->Update(); }                                   // 24: 00604130
    void CreateScreen() override { AsScreen()->Create(); }                             // 25: 006024B0
    void CloseScreen() override { AsScreen()->Close(); }                               // 26: 00602BB0
    void MessageResult(int result) override { AsScreen()->OnMessageResult(static_cast<std::uint32_t>(result)); }   // 27: 00606F40
    void UnitJoined(CUnit* unit) override { AsScreen()->OnUnitJoined(unit); }          // 29: 00602C90
    void UnitLeft(CUnit* unit) override { AsScreen()->OnUnitLeft(unit); }              // 30: 00603010
    void GameOver(int won) override { AsScreen()->OnGameOver(won); }                   // 31: 00608C70
    void ShowBriefing(const char* name) override { AsScreen()->OnBriefing(name); }     // 34: 006030C0
    bool ReadyToStopPolling() override { return true; }                                // 35: 00608D10 (AL 1)
    void PlayersChanged(int show) override { AsScreen()->ShowChat(static_cast<char>(show)); }   // 36: 00603090
    void PlayerList(int mode) override { AsScreen()->SetChatActive(mode); }            // 38: 005F6B70
    void SaveState() override { AsScreen()->SaveState(); }                             // 40: 00602490
};
struct BriefingScreenInputClass final : ContainerInputClass<BriefingScreenInputClass, &CBriefingScreen::BriefingDelete> {
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnLeftDown(b, x, y), 0; }         // 2: 006030F0
    int MouseMove(int b, int x, int y) override { return AsScreen()->OnMouseMove(b, x, y), 0; }       // 4: 00603D00
    int LeftDouble(int b, int x, int y) override { return AsScreen()->OnLeftDouble(b, x, y), 0; }     // 7: 00603360
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }           // 9: 006034A0
};

// --- The view screen, screen 7 (00740840 / 0074080C) -----------------------------------------------

struct ViewScreenClass final : ContainerClass<ViewScreenClass, &CUiContainer::UiContainerDelete, CViewScreen> {
    BOOL Draw(int pass) override { return AsScreen()->DrawView(pass); }   // 20: 0062AA50
    void CreateScreen() override { AsScreen()->Build(); }                 // 25: 0062A830
    void CloseScreen() override { AsScreen()->OnClose(); }                // 26: 0062AA40
};
// Its input side is the container's (0074080C: every handler the container's, slot 0 the container's delete).
struct ViewScreenInputClass final : ContainerInputClass<ViewScreenInputClass, &CUiContainer::UiContainerDelete, CViewScreen> {};

// --- The island map, screen 4 (00740B3C / 00740B08) ------------------------------------------------

struct GlobalMapClass final : ContainerClass<GlobalMapClass, &CGlobalMap::GlobalMapDelete> {
    BOOL Draw(int pass) override { return Container()->DrawBlendedImpl(pass); }        // 20: 0062EFC0
    void Update() override { AsScreen()->Update(); }                                   // 24: 0062F050
    void CreateScreen() override { AsScreen()->Create(); }                             // 25: 0062D350
    void CloseScreen() override { AsScreen()->Close(); }                               // 26: 0062EDD0
    void MessageResult(int result) override { AsScreen()->OnMessageResult(result); }   // 27: 0062EE30
    void SaveState() override { AsScreen()->SaveState(); }                             // 40: 0062D330
    const char* HelpTopic() override { return AsScreen()->HelpTopic(); }               // 41: 0062EE70
};
struct GlobalMapInputClass final : ContainerInputClass<GlobalMapInputClass, &CGlobalMap::GlobalMapDelete> {
    void Deactivate() override { AsScreen()->OnDeactivate(); }                                      // 1: 0062FF10
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnLeftDown(b, x, y), 0; }       // 2: 0062F950
    int LeftUp(int b, int x, int y) override { return AsScreen()->OnLButtonUp(b, x, y); }           // 3: 0062FEF0
    int MouseMove(int b, int x, int y) override { return AsScreen()->OnMouseMove(b, x, y), 0; }     // 4: 00630010
    int RightDown(int b, int x, int y) override { return AsScreen()->OnRightDown(b, x, y), 0; }     // 5: 0062FF60
    int RightUp(int b, int x, int y) override { return AsScreen()->OnRightUp(b, x, y), 0; }         // 6: 0062FFC0
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }         // 9: 00630580
};

// --- The titles, screen 8 (00740C3C / 00740C08) ----------------------------------------------------

struct TitlesClass final : ContainerClass<TitlesClass, &CTitlesScreen::TitlesDelete, CCreditsScreen> {
    BOOL Draw(int pass) override { return AsScreen()->Draw(pass); }                    // 20: 0062BD80
    void CreateScreen() override { AsScreen()->Build(); }                              // 25: 0062B7D0
    void CloseScreen() override { AsScreen()->OnClose(); }                             // 26: 0062BD00
    void MessageResult(int result) override { AsScreen()->OnMessageResult(result); }   // 27: 0062BFD0
};
struct TitlesInputClass final : ContainerInputClass<TitlesInputClass, &CTitlesScreen::TitlesDelete, CCreditsScreen> {
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnLButtonDown(b, x, y), 0; }   // 2: 0062BFE0
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }        // 9: 0062C010
};

// --- The help / message window (00740E28 / 00740DF4) -----------------------------------------------

struct HelpWindowClass final : ContainerClass<HelpWindowClass, &CHelpWindow::HelpWindowDelete> {
    BOOL Draw(int pass) override { return Container()->DrawAlphaRefImpl(pass); }   // 20: 0065A6D0
    void Update() override { AsScreen()->Update(); }                               // 24: 0065A5E0
    void CreateScreen() override { AsScreen()->Build(); }                          // 25: 00658E10
    void CloseScreen() override { AsScreen()->Close(); }                           // 26: 006590F0
};
struct HelpWindowInputClass final : ContainerInputClass<HelpWindowInputClass, &CHelpWindow::HelpWindowDelete> {
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnLeftDown(b, x, y), 0; }   // 2: 00659190
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }     // 9: 006592C0
};

// --- The objectives window (007411BC / 00741188) ---------------------------------------------------

struct ObjectivesClass final : ContainerClass<ObjectivesClass, &CObjectivesWindow::ObjectivesDelete> {
    void Update() override { AsScreen()->Update(); }                       // 24: 0064E760
    void CreateScreen() override { AsScreen()->Build(); }                  // 25: 0064BE80
    void CloseScreen() override { AsScreen()->RestoreAlphaRef(); }         // 26: 0064D970
    const char* HelpTopic() override { return AsScreen()->HelpTopic(); }   // 41: 0064D990
};
struct ObjectivesInputClass final : ContainerInputClass<ObjectivesInputClass, &CObjectivesWindow::ObjectivesDelete> {
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnLButtonDown(b, x, y), 0; }   // 2: 0064E330
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }        // 9: 0064E5A0
};

// --- The Esc menu (007412A4 / 00741270) ------------------------------------------------------------

struct EscMenuClass final : ContainerClass<EscMenuClass, &CUiContainer::UiContainerDelete, CEscMenu> {
    void CreateScreen() override { AsScreen()->Build(); }                              // 25: 0062AB20
    void CloseScreen() override { AsScreen()->OnClose(); }                             // 26: 0062ACB0
    void MessageResult(int result) override { AsScreen()->OnMessageResult(result); }   // 27: 0062ACC0
};
struct EscMenuInputClass final : ContainerInputClass<EscMenuInputClass, &CUiContainer::UiContainerDelete, CEscMenu> {
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnLButtonDown(b, x, y), 0; }   // 2: 0062AE90
    int MouseMove(int b, int x, int y) override { return AsScreen()->OnMouseMove(b, x, y), 0; }    // 4: 0062AD10
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }        // 9: 0062B300
};

// --- The save / load window (00741388 / 00741354) --------------------------------------------------

struct SaveLoadClass final : ContainerClass<SaveLoadClass, &CSaveLoadWindow::SaveLoadDelete> {
    BOOL Draw(int pass) override { return AsScreen()->Draw(pass); }                   // 20: 006544A0
    void Update() override { AsScreen()->Update(); }                                  // 24: 006545E0
    void CreateScreen() override { AsScreen()->Build(); }                             // 25: 00653DD0
    void CloseScreen() override { AsScreen()->CloseDialog(); }                        // 26: 00654430
    void MessageResult(int result) override { AsScreen()->OnDialogResult(result); }   // 27: 00654450
};
struct SaveLoadInputClass final : ContainerInputClass<SaveLoadInputClass, &CSaveLoadWindow::SaveLoadDelete> {
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnMouseDown(b, x, y), 0; }       // 2: 00654660
    int LeftDouble(int b, int x, int y) override { return AsScreen()->OnDoubleClick(b, x, y), 0; }   // 7: 006547B0
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }          // 9: 006548A0
    int Char(int k, int r, int f) override { return AsScreen()->OnChar(k, r, f), 0; }                // 10: 00655050
};

// --- The options window (00741FAC / 00741F78) ------------------------------------------------------

struct OptionsClass final : ContainerClass<OptionsClass, &COptionsWindow::OptionsWindowDelete> {
    BOOL Draw(int pass) override { return AsScreen()->Draw(pass); }                   // 20: 00650920
    void Redraw() override { AsScreen()->Redraw(); }                                  // 22: 00650A20
    void Update() override { AsScreen()->Update(); }                                  // 24: 00650A30
    void CreateScreen() override { AsScreen()->Build(); }                             // 25: 006503C0
    void CloseScreen() override {}                                                    // 26: 006508D0 (empty)
    void MessageResult(int result) override { AsScreen()->OnDialogResult(result); }   // 27: 006508E0
    const char* HelpTopic() override { return AsScreen()->HelpTopic(); }              // 41: 00650900
};
struct OptionsInputClass final : ContainerInputClass<OptionsInputClass, &COptionsWindow::OptionsWindowDelete> {
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnMouseDown(b, x, y), 0; }       // 2: 00650C20
    int LeftDouble(int b, int x, int y) override { return AsScreen()->OnDoubleClick(b, x, y), 0; }   // 7: 00650D70
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }          // 9: 00650E40
};

// --- The message box (00742348 / 00742314), the difficulty question (0074225C / 00742228) and the master server
// request box (00741DD8 / 00741DA4) ----------------------------------------------------------------------------

template <typename Self, auto Delete, typename Box>
class MessageBoxBaseClass : public ContainerClass<Self, Delete, Box> {
public:
    void CreateScreen() override { this->AsScreen()->Build(); }       // 25: 00634790 (the difficulty question 006337B0)
    void CloseScreen() override { this->AsScreen()->OnClose(); }      // 26: 00634D00
};
template <typename Self, auto Delete, typename Box>
class MessageBoxInputBaseClass : public ContainerInputClass<Self, Delete, Box> {
public:
    int LeftDown(int b, int x, int y) override { return this->AsScreen()->OnLButtonDown(b, x, y), 0; }   // 2: 00634D20 (00633DD0)
    int KeyDown(int k, int r, int f) override { return this->AsScreen()->OnKeyDown(k, r, f), 0; }        // 9: 00634E60 (00633EC0)
};
struct MessageBoxClass final : MessageBoxBaseClass<MessageBoxClass, &CMessageBox::MessageBoxDelete, CMessageBox> {};
struct MessageBoxInputClass final : MessageBoxInputBaseClass<MessageBoxInputClass, &CMessageBox::MessageBoxDelete, CMessageBox> {};
struct DifficultyBoxClass final : MessageBoxBaseClass<DifficultyBoxClass, &CMessageBox::MessageBoxDelete, CDifficultyBox> {};
struct DifficultyBoxInputClass final
    : MessageBoxInputBaseClass<DifficultyBoxInputClass, &CMessageBox::MessageBoxDelete, CDifficultyBox> {};
struct MasterRequestBoxClass final
    : MessageBoxBaseClass<MasterRequestBoxClass, &CMasterRequestBox::MasterRequestBoxDelete, CMessageBox> {
    void Update() override { static_cast<CMasterRequestBox*>(AsScreen())->Update(); }   // 24: 0061E4E0
};
struct MasterRequestBoxInputClass final
    : MessageBoxInputBaseClass<MasterRequestBoxInputClass, &CMasterRequestBox::MasterRequestBoxDelete, CMessageBox> {};

// --- The multiplayer connection screen (00741CD0 / 00741C9C) ----------------------------------------

struct ConnectScreenClass final : ContainerClass<ConnectScreenClass, &CMpConnectScreen::ConnectScreenDelete> {
    void Update() override { AsScreen()->Update(); }                                                     // 24: 0061C430
    void CreateScreen() override { AsScreen()->Create(); }                                               // 25: 0061B2C0
    void CloseScreen() override {}                                                                       // 26: 0061C2D0 (empty)
    void MessageResult(int result) override { AsScreen()->OnBoxResult(static_cast<std::uint32_t>(result)); }   // 27: 0061C2E0
};
struct ConnectScreenInputClass final : ContainerInputClass<ConnectScreenInputClass, &CMpConnectScreen::ConnectScreenDelete> {
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnLButtonDown(b, x, y), 0; }       // 2: 0061C700
    int LeftDouble(int b, int x, int y) override { return AsScreen()->OnLButtonDouble(b, x, y), 0; }   // 7: 0061CA30
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }            // 9: 0061CAD0
};

// --- The item screens: the plain one (00740A2C / 007409F8), the camp (0074091C / 007408E8), the exchange (00740D18 /
// 00740CE4) and the multiplayer hero screen (0074214C / 00742118) --------------------------------------------------

struct ItemScreenBaseClass final : ItemScreenClass<ItemScreenBaseClass, &CItemScreen::ItemScreenDelete> {};
struct ItemScreenBaseInputClass final : ContainerInputClass<ItemScreenBaseInputClass, &CItemScreen::ItemScreenDelete> {};

struct CampClass final : ItemScreenClass<CampClass, &CItemScreen::CampScreenDelete, CCampPages> {
    BOOL Draw(int pass) override { return AsScreen()->Draw(pass); }                    // 20: 0060F050
    void Update() override { AsScreen()->Update(); }                                   // 24: 0060F6B0
    void CreateScreen() override { AsScreen()->Create(); }                             // 25: 0060C870
    void CloseScreen() override { AsScreen()->Close(); }                               // 26: 0060E6B0
    void MessageResult(int result) override { AsScreen()->OnMessageResult(result); }   // 27: 0060E900
    void DebugFill() override { AsScreen()->DebugFill(); }                             // 39: 00612CF0
    const char* HelpTopic() override { return AsScreen()->HelpTopic(); }               // 41: 0060E980
    void TakeItem(CItem* item, int mode) override { AsScreen()->MoveItem(item, mode); }              // 42: 00610AD0
    bool CanEquip(CItem* item, int mode) override { return AsScreen()->CanEquip(item, mode); }       // 43: 006100E0
    void TopicClicked(int page, int unused) override { Camp()->ShowPageTabs(page, unused); }         // 44: 006110F0
    void BestPartySkill(int school, int* skill, int* mana) override { Camp()->BestPartySkill(school, skill, mana); }   // 45
    void RefreshWeapons() override { AsScreen()->RefreshWeapons(); }                   // 46: 0060F9E0
    void RefreshStats() override { AsScreen()->RefreshStats(); }                       // 47: 0060F980
    void NextHero() override { AsScreen()->NextHero(); }                               // 48: 0060F710
    void PreviousHero() override { AsScreen()->PreviousHero(); }                       // 49: 0060F7A0
    CUnit* HeroUnit(int index) override { return AsScreen()->HeroAt(index); }          // 50: 005F65F0
    PartyUnitRecord* HeroRecord() override { return AsScreen()->HeroRecord(); }        // 51: 0060FA80
    int Price(CItem* item, int kind) override { return AsScreen()->Price(item, kind); }   // 54: 0060FAB0

private:
    // The camp's slots 44 / 45 (006110F0, 00610020) are written over its window as CCampScreen (ui_screens.h).
    CCampScreen* Camp() { return ObjectAs<CCampScreen>(); }
};
struct CampInputClass final : ContainerInputClass<CampInputClass, &CItemScreen::CampScreenDelete, CCampPages> {
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnLButtonDown(b, x, y), 0; }   // 2: 0060F470
    int LeftUp(int b, int x, int y) override { return AsScreen()->OnLButtonUp(b, x, y), 0; }       // 3: 0060F5B0
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }        // 9: 0060F600
    int KeyUp(int k, int r, int f) override { return AsScreen()->OnKeyUp(k, r, f), 0; }            // 11: 0060F690
};

struct ExchangeClass final : ItemScreenClass<ExchangeClass, &CItemScreen::Screen9Delete, CExchangeScreen> {
    BOOL Draw(int pass) override { return AsScreen()->Draw(pass); }                    // 20: 0062C9C0
    void Update() override { AsScreen()->Update(); }                                   // 24: 0062C930
    void CreateScreen() override { AsScreen()->Create(); }                             // 25: 0062C250
    void CloseScreen() override { AsScreen()->Close(); }                               // 26: 0062C8F0
    void MessageResult(int result) override { AsScreen()->OnMessageResult(result); }   // 27: 0062C920
    void ExchangeDone(CParty* partyB, CParty* partyA, CParty* partyC) override {        // 37: 0062D210
        AsScreen()->OnExchangeDone(partyB, partyA, partyC);
    }
    void TakeItem(CItem* item, int mode) override { AsScreen()->MoveOffered(item, mode); }          // 42: 0062D100
    bool CanEquip(CItem* item, int mode) override { return AsScreen()->CanMove(item, mode); }       // 43: 0062D200
};
struct ExchangeInputClass final : ContainerInputClass<ExchangeInputClass, &CItemScreen::Screen9Delete, CExchangeScreen> {
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnLButtonDown(b, x, y), 0; }   // 2: 0062CF70
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }        // 9: 0062D090
};

struct MpHeroScreenClass final : ItemScreenClass<MpHeroScreenClass, &CMpHeroScreen::HeroScreenDelete> {
    BOOL Draw(int pass) override { return AsScreen()->Draw(pass); }                    // 20: 006159E0
    void Update() override { AsScreen()->Update(); }                                   // 24: 00617820
    void CreateScreen() override { AsScreen()->Create(); }                             // 25: 00613EA0
    void CloseScreen() override { AsScreen()->Close(); }                               // 26: 00615850
    void MessageResult(int result) override { AsScreen()->OnMessageResult(result); }   // 27: 00615940
    void TakeItem(CItem* item, int mode) override { AsScreen()->OnItemChosen(item, mode); }         // 42: 00618980
    bool CanEquip(CItem* item, int mode) override { return AsScreen()->CanEquip(item, mode); }      // 43: 006188E0
    void RefreshStats() override { AsScreen()->RefreshHero(); }                        // 47: 00618850
    PartyUnitRecord* HeroRecord() override { return AsScreen()->GetHero(); }           // 51: 00616900
    void ChooseFigure(const char* prototype) override { AsScreen()->ChooseFigure(prototype); }      // 52: 00616AC0
    double Weight() override { return AsScreen()->EquipmentWeight(); }                 // 53: 006193C0
};
struct MpHeroScreenInputClass final : ContainerInputClass<MpHeroScreenInputClass, &CMpHeroScreen::HeroScreenDelete> {
    int LeftDown(int b, int x, int y) override { return AsScreen()->OnLButtonDown(b, x, y), 0; }   // 2: 00617050
    int LeftUp(int b, int x, int y) override { return AsScreen()->OnLButtonUp(b, x, y), 0; }       // 3: 006177D0
    int KeyDown(int k, int r, int f) override { return AsScreen()->OnKeyDown(k, r, f), 0; }        // 9: 00618A10
};

template <typename Tag>
void** Table() {
    return Tag::Slots();
}

} // namespace

void** CUiContainer::ContainerVtable() { return Table<ContainerBaseClass>(); }
void** CUiContainer::ContainerInputVtable() { return Table<ContainerBaseInputClass>(); }
void** CBattleScreen::Vtable() { return Table<BattleScreenClass>(); }
void** CBattleScreen::InputVtable() { return Table<BattleScreenInputClass>(); }
void** CMainMenu::Vtable() { return Table<MainMenuClass>(); }
void** CMainMenu::InputVtable() { return Table<MainMenuInputClass>(); }
void** CBriefingScreen::Vtable() { return Table<BriefingScreenClass>(); }
void** CBriefingScreen::InputVtable() { return Table<BriefingScreenInputClass>(); }
void** CViewScreen::Vtable() { return Table<ViewScreenClass>(); }
void** CViewScreen::InputVtable() { return Table<ViewScreenInputClass>(); }
void** CGlobalMap::Vtable() { return Table<GlobalMapClass>(); }
void** CGlobalMap::InputVtable() { return Table<GlobalMapInputClass>(); }
void** CCreditsScreen::Vtable() { return Table<TitlesClass>(); }
void** CCreditsScreen::InputVtable() { return Table<TitlesInputClass>(); }
void** CHelpWindow::Vtable() { return Table<HelpWindowClass>(); }
void** CHelpWindow::InputVtable() { return Table<HelpWindowInputClass>(); }
void** CObjectivesWindow::Vtable() { return Table<ObjectivesClass>(); }
void** CObjectivesWindow::InputVtable() { return Table<ObjectivesInputClass>(); }
void** CEscMenu::Vtable() { return Table<EscMenuClass>(); }
void** CEscMenu::InputVtable() { return Table<EscMenuInputClass>(); }
void** CSaveLoadWindow::Vtable() { return Table<SaveLoadClass>(); }
void** CSaveLoadWindow::InputVtable() { return Table<SaveLoadInputClass>(); }
void** COptionsWindow::Vtable() { return Table<OptionsClass>(); }
void** COptionsWindow::InputVtable() { return Table<OptionsInputClass>(); }
void** CMessageBox::Vtable() { return Table<MessageBoxClass>(); }
void** CMessageBox::InputVtable() { return Table<MessageBoxInputClass>(); }
void** CDifficultyBox::Vtable() { return Table<DifficultyBoxClass>(); }
void** CDifficultyBox::InputVtable() { return Table<DifficultyBoxInputClass>(); }
void** CMasterRequestBox::Vtable() { return Table<MasterRequestBoxClass>(); }
void** CMasterRequestBox::InputVtable() { return Table<MasterRequestBoxInputClass>(); }
void** CMpConnectScreen::Vtable() { return Table<ConnectScreenClass>(); }
void** CMpConnectScreen::InputVtable() { return Table<ConnectScreenInputClass>(); }
void** CItemScreen::ItemScreenVtable() { return Table<ItemScreenBaseClass>(); }
void** CItemScreen::ItemScreenInputVtable() { return Table<ItemScreenBaseInputClass>(); }
void** CCampPages::Vtable() { return Table<CampClass>(); }
void** CCampPages::InputVtable() { return Table<CampInputClass>(); }
void** CExchangeScreen::Vtable() { return Table<ExchangeClass>(); }
void** CExchangeScreen::InputVtable() { return Table<ExchangeInputClass>(); }
void** CMpHeroScreen::Vtable() { return Table<MpHeroScreenClass>(); }
void** CMpHeroScreen::InputVtable() { return Table<MpHeroScreenInputClass>(); }
