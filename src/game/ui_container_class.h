#pragma once

// Compiled tables of the containers and the screens (C19; docs/notes/standalone.md, "Compiler-made vtables"). A
// container (CUiContainer: a window with child windows and extra input sides) dispatches through game.exe's 42-slot
// tables (the item screens' through 55-slot ones); a tag over ContainerClass declares the same slots, so a screen's
// constructor stores `Tag::Vtable()` in place of game.exe's table. The defaults are the container's (game.exe's
// 00741008): the window's slots 0-18 (ui_window_class.h), 19 ShowChildren, 20 DrawContainerImpl, 21
// DrawChildrenImpl, 22 RedrawContainer, 23 RedrawChildTips, 24 UpdateBase, then the screen slots the interface calls
// (25 / 26 pure, 27-41 game.exe's shared RET stubs 005795A0-00579680). `Screen` is the class whose bodies a tag's
// slots call (AsScreen()): the window class by default, a class derived from it, or a screen kept as a struct around
// its container (CEscMenu, CMpHeroScreen).

#include "ui.h"
#include "ui_input_class.h"
#include "ui_item_info.h"
#include "ui_window_class.h"

#include <type_traits>

extern "C" int __cdecl _purecall();

class CItem;
class CParty;
class CUnit;
struct PartyUnitRecord;

// `window` as `To`: a class derived from From by static_cast, a screen kept as a struct around its container by
// reinterpret_cast (its window is its first member).
template <typename To, typename From>
To* ScreenAs(From* window) {
    if constexpr (std::is_base_of_v<From, To>) return static_cast<To*>(window);
    else return reinterpret_cast<To*>(window);
}

template <typename Self, auto Delete, typename Screen = std::remove_pointer_t<decltype(UiWindowDeleteType(Delete))>>
class ContainerClass : public UiWindowClass<Self, Delete> {
public:
    void Show(int show) override { Container()->ShowChildren(show); }                  // 19: 00601AC0
    BOOL Draw(int pass) override { return Container()->DrawContainerImpl(pass); }      // 20: 00600EF0
    void DrawChild(int pass) override { Container()->DrawChildrenImpl(pass); }         // 21: 00600F70
    void Redraw() override { Container()->RedrawContainer(); }                         // 22: 00601040
    void RedrawTips() override { Container()->RedrawChildTips(); }                     // 23: 00601090
    void Update() override { Container()->UpdateBase(); }                              // 24: 006010E0
    virtual void CreateScreen() { _purecall(); }                                      // 25: pure (the screen's Create)
    virtual void CloseScreen() { _purecall(); }                                       // 26: pure (taken off the screen)
    virtual void MessageResult(int) {}                                                // 27: 005795A0 (RET 4)
    virtual void ItemsPicked(void*, int, int) {}                                      // 28: 005795B0 (RET 0xC)
    virtual void UnitJoined(CUnit*) {}                                                // 29: 005795C0 (RET 4)
    virtual void UnitLeft(CUnit*) {}                                                  // 30: 005795D0 (RET 4)
    virtual void GameOver(int) {}                                                     // 31: 005795E0 (RET 4)
    virtual void Speech(int, unsigned, char*) {}                                      // 32: 005795F0 (RET 0xC)
    virtual void QuestVariable(const char*, float) {}                                 // 33: 00579600 (RET 8)
    virtual void ShowBriefing(const char*) {}                                         // 34: 00579610 (RET 4)
    virtual bool ReadyToStopPolling() { return false; }                               // 35: 00579620 (AL 0)
    virtual void PlayersChanged(int) {}                                               // 36: 00579630 (RET 4)
    virtual void ExchangeDone(CParty*, CParty*, CParty*) {}                           // 37: 00579640 (RET 0xC)
    virtual void PlayerList(int) {}                                                   // 38: 00579650 (RET 4)
    virtual void DebugFill() {}                                                       // 39: 00579660 (RET)
    virtual void SaveState() {}                                                       // 40: 00579670 (RET)
    virtual const char* HelpTopic() { return nullptr; }                               // 41: 00579680 (0)

protected:
    CUiContainer* Container() { return this->template ObjectAs<CUiContainer>(); }
    Screen* AsScreen() { return this->template ObjectAs<Screen>(); }
};

// The item screens' slots over the container's (game.exe's plain item screen 00740A2C, whose 25 / 26 are pure; the
// camp 0074091C, the exchange 00740D18 and the hero screen 0074214C override them): what the item strips and panels
// call on their owner. The defaults are CItemScreen's answers (ui_item_info.h) and empty RET stubs.
template <typename Self, auto Delete, typename Screen = std::remove_pointer_t<decltype(UiWindowDeleteType(Delete))>>
class ItemScreenClass : public ContainerClass<Self, Delete, Screen> {
public:
    virtual void TakeItem(CItem*, int) {}                                                     // 42: 005F6370 (RET 8)
    virtual bool CanEquip(CItem* item, int mode) { return Items()->CanEquipDefault(item, mode); }   // 43: 005F6380
    virtual void TopicClicked(int, int) {}                                                    // 44: 005F6390 (RET 8)
    virtual void BestPartySkill(int school, int* skill, int* mana) {                          // 45: 005F63A0
        Items()->BestPartySkillDefault(school, skill, mana);
    }
    virtual void RefreshWeapons() {}                                                          // 46: 005F63C0
    virtual void RefreshStats() {}                                                            // 47: 005F63D0
    virtual void NextHero() {}                                                                // 48: 005F63E0
    virtual void PreviousHero() {}                                                            // 49: 005F63F0
    virtual CUnit* HeroUnit(int index) { return Items()->HeroUnitDefault(index); }   // 50: 005F6400
    virtual PartyUnitRecord* HeroRecord() { return nullptr; }                                 // 51: 005F6410
    virtual void ChooseFigure(const char*) {}                                                 // 52: 005F6420 (RET 4)
    virtual double Weight() { return Items()->WeightDefault(); }                              // 53: 005F6430 (ST0)
    virtual int Price(CItem* item, int kind) { return Items()->PriceDefault(item, kind); }    // 54: 005F6440

protected:
    CItemScreen* Items() { return reinterpret_cast<CItemScreen*>(this); }
};

// A container's input side (game.exe's 00740FD4 and the screens' 13-slot input tables): every handler passes the
// event on to the extra input sides and the children (UiContainerInput); a screen's tag overrides the handlers its
// table has of its own, on AsScreen().
template <typename Self, auto Delete, typename Screen = std::remove_pointer_t<decltype(InputSideWindowType(Delete))>>
class ContainerInputClass : public InputSideClass<Self, Delete> {
public:
    UiContainerInput* Input() { return static_cast<UiContainerInput*>(this->Side()); }
    Screen* AsScreen() { return ScreenAs<Screen>(this->SideWindow()); }
    void Deactivate() override { Input()->OnDeactivate(); }                                       // 1: 00601A60
    int LeftDown(int b, int x, int y) override { return Input()->OnLeftDown(b, x, y), 0; }         // 2: 00601110
    int LeftUp(int b, int x, int y) override { return Input()->OnLeftUp(b, x, y), 0; }             // 3: 006011F0
    int MouseMove(int b, int x, int y) override { return Input()->OnMouseMove(b, x, y), 0; }       // 4: 006012D0
    int RightDown(int b, int x, int y) override { return Input()->OnRightDown(b, x, y), 0; }       // 5: 006013B0
    int RightUp(int b, int x, int y) override { return Input()->OnRightUp(b, x, y), 0; }           // 6: 00601490
    int LeftDouble(int b, int x, int y) override { return Input()->OnLeftDouble(b, x, y), 0; }     // 7: 00601570
    int RightDouble(int b, int x, int y) override { return Input()->OnRightDouble(b, x, y), 0; }   // 8: 00601650
    int KeyDown(int k, int r, int f) override { return Input()->OnKeyDown(k, r, f), 0; }           // 9: 00601820
    int Char(int k, int r, int f) override { return Input()->OnChar(k, r, f), 0; }                 // 10: 006018E0
    int KeyUp(int k, int r, int f) override { return Input()->OnKeyUp(k, r, f), 0; }               // 11: 006019A0
    int Wheel(int f, int d, int x, int y) override { return Input()->OnWheel(f, d, x, y); }        // 12: 00601730
};
