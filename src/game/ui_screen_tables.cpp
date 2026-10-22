// The containers' and screens' compiled tables (C19; ui_container_class.h, docs/notes/standalone.md "Compiler-made
// vtables"): a tag per game.exe table the screens' constructors stored, each with the slots its table has of its own;
// the accessors the constructors and the harness call. game.exe's addresses are in the comments.
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_container_class.h"
#include "ui_item_info.h"
#include "ui_screens.h"
#include "ui_view_screen.h"

namespace {

// --- The item screens: the plain one (00740A2C / 007409F8), the camp (0074091C / 007408E8), the exchange (00740D18 /
// 00740CE4) and the multiplayer hero screen (0074214C / 00742118) --------------------------------------------------

struct ItemScreenBaseClass final : ItemScreenClass<ItemScreenBaseClass, &CItemScreen::ItemScreenDelete> {};
struct ItemScreenBaseInputClass final : ContainerInputClass<ItemScreenBaseInputClass, &CItemScreen::ItemScreenDelete> {};

struct CampClass final : ItemScreenClass<CampClass, &CItemScreen::CampScreenDelete, CCampPages> {
    BOOL Draw(int pass) override { return AsScreen()->ScreenType::Draw(pass); }                    // 20: 0060F050
    void Update() override { AsScreen()->ScreenType::Update(); }                                   // 24: 0060F6B0
    void CreateScreen() override { AsScreen()->ScreenType::CreateScreen(); }                             // 25: 0060C870
    void CloseScreen() override { AsScreen()->Close(); }                               // 26: 0060E6B0
    void MessageResult(int result) override { AsScreen()->OnMessageResult(result); }   // 27: 0060E900
    void DebugFill() override { AsScreen()->ScreenType::DebugFill(); }                             // 39: 00612CF0
    const char* HelpTopic() override { return AsScreen()->ScreenType::HelpTopic(); }               // 41: 0060E980
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
    BOOL Draw(int pass) override { return AsScreen()->ScreenType::Draw(pass); }                    // 20: 0062C9C0
    void Update() override { AsScreen()->ScreenType::Update(); }                                   // 24: 0062C930
    void CreateScreen() override { AsScreen()->ScreenType::CreateScreen(); }                             // 25: 0062C250
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
    BOOL Draw(int pass) override { return AsScreen()->ScreenType::Draw(pass); }                    // 20: 006159E0
    void Update() override { AsScreen()->ScreenType::Update(); }                                   // 24: 00617820
    void CreateScreen() override { AsScreen()->ScreenType::CreateScreen(); }                             // 25: 00613EA0
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

void** CItemScreen::ItemScreenVtable() { return Table<ItemScreenBaseClass>(); }
void** CItemScreen::ItemScreenInputVtable() { return Table<ItemScreenBaseInputClass>(); }
void** CCampPages::Vtable() { return Table<CampClass>(); }
void** CCampPages::InputVtable() { return Table<CampInputClass>(); }
void** CExchangeScreen::Vtable() { return Table<ExchangeClass>(); }
void** CExchangeScreen::InputVtable() { return Table<ExchangeInputClass>(); }
void** CMpHeroScreen::Vtable() { return Table<MpHeroScreenClass>(); }
void** CMpHeroScreen::InputVtable() { return Table<MpHeroScreenInputClass>(); }
