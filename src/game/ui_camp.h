#pragma once

// The remaining interface screens of 00600000-00660000 (see docs/notes/ui.md, "Camp, global map
// and battle screen setup"): the battle screen's Create, the global map, the camp's pages and
// the hero panels, the credits. Names provisional. The classes here describe only the fields
// the rewritten code uses; the objects are created by original code (sizes asserted where known).

#include "ui.h"
#include "ui_screens.h"

#include <cstddef>
#include <cstdint>

// An MFC CArray<DWORD>-like array (vtable, data, size, capacity, growth).
struct UiDwordArray {
    void* vtable_;
    std::uint32_t* data_;
    int size_;
    int capacity_;
    int growBy_;
    // 0059C660 (not rewritten; an MFC SetSize instantiation). `growBy` -1 keeps the step.
    void SetSize(int size, int growBy);
};
static_assert(sizeof(UiDwordArray) == 0x14);

// --- Battle screen --------------------------------------------------------------------------
//
// The in-game screen (CreateScreen id 1, vtables 00740764 / 00740730, 0x230 bytes). Its Create
// (slot 25, 00635250) builds the HUD panels as children (ui_battle_setup.cpp). Children, in
// order: weapon bar, quick items, spell column, action column, portraits, unit panel, map,
// clock, movement panel, message log, top panel.
class CBattleScreenSetup : public CUiContainer {
public:
    // 00635250 (slot 25). Reads the screen's saved state when a game is being loaded, else
    // remembers the local player's first hero; creates the panels and the "pers" texts table.
    void Create();

    int unknown_164;                 // +0x164 cleared by Create
    std::uint8_t partyArray_[0x14];  // +0x168 the party units (CArray; SetSize 005EBDB0)
    int unknown_17C;                 // +0x17C 0
    int unknown_180;                 // +0x180 -1
    std::uint8_t unknown_184[4];
    int savedSelection_;             // +0x188 -1, or read from the save (version 0x62)
    std::uint8_t savedFlag_;         // +0x18C
    std::uint8_t unknown_18D[0x194 - 0x18D];   // +0x18D dword, +0x191 word cleared by Create
    CUiWindow* weaponBar_;           // +0x194
    CUiWindow* quickItems_;          // +0x198
    CUiWindow* spellColumn_;         // +0x19C
    CUiWindow* actionColumn_;        // +0x1A0
    CUiWindow* portraits_;           // +0x1A4
    CUiWindow* unitPanel_;           // +0x1A8
    CUiWindow* map_;                 // +0x1AC
    CUiWindow* clock_;               // +0x1B0
    CUiWindow* movePanel_;           // +0x1B4
    CUiWindow* messageLog_;          // +0x1B8
    CUiWindow* topPanel_;            // +0x1BC
    bool unknown_1C0;                // +0x1C0
    bool unknown_1C1;                // +0x1C1
    std::uint8_t unknown_1C2[0x1DC - 0x1C2];
    UiDwordArray heroes_;            // +0x1DC ids of the heroes shown (saved with the game)
    void* personTexts_;              // +0x1F0 STLport map<int, CString>: the "pers" texts by person
    std::uint8_t unknown_1F4[0x1FC - 0x1F4];
    int unknown_1FC;                 // +0x1FC 0
    bool hasHero_;                   // +0x200 a hero id was added (new game)
    std::uint8_t unknown_201[0x210 - 0x201];
    int unknown_210;                 // +0x210 -1
    std::uint8_t unknown_214[0x22C - 0x214];
    int portraitMode_;               // +0x22C 0
};
static_assert(offsetof(CBattleScreenSetup, partyArray_) == 0x168);
static_assert(offsetof(CBattleScreenSetup, savedSelection_) == 0x188);
static_assert(offsetof(CBattleScreenSetup, weaponBar_) == 0x194);
static_assert(offsetof(CBattleScreenSetup, topPanel_) == 0x1BC);
static_assert(offsetof(CBattleScreenSetup, heroes_) == 0x1DC);
static_assert(offsetof(CBattleScreenSetup, personTexts_) == 0x1F0);
static_assert(offsetof(CBattleScreenSetup, hasHero_) == 0x200);
static_assert(offsetof(CBattleScreenSetup, unknown_210) == 0x210);
static_assert(offsetof(CBattleScreenSetup, portraitMode_) == 0x22C);
