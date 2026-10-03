#pragma once

// The remaining interface screens of 00600000-00660000 (see docs/notes/ui.md, "Camp, global map
// and battle screen setup"): the battle screen's Create, the global map, the camp's pages and
// the hero panels, the credits. Names provisional. The classes here describe only the fields
// the rewritten code uses; the objects are created by original code (sizes asserted where known).

#include "object_tables.h"
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

// --- Global map -----------------------------------------------------------------------------
//
// The island map shown between zones (CreateScreen id 4, vtables 00740B08 / 00740B3C, 0x294 bytes;
// ui_global_map.cpp). Regions: 0 / 1 turn the view, 2 the camp, 3.. the links between zones.

// A zone of the allod on the map (0x10 bytes, CArray at +0x18C).
struct GlobalMapZone {
    int state;                       // +0x00 the player's "z.<zone>" variable, truncated
    int link;                        // +0x04 -1, or the link that leads to it (state 2)
    char* name;                      // +0x08 CString
    char* title;                     // +0x0C CString
};
static_assert(sizeof(GlobalMapZone) == 0x10);

// A quest place or a link (an "edge" zone) on the map (0x18 bytes, CArrays at +0x1A0 / +0x1B4).
struct GlobalMapPlace {
    int zone;                        // +0x00 index into the zones
    float x, y, z;                   // +0x04 position on the map model
    char* name;                      // +0x10 CString
    char* title;                     // +0x14 CString
};
static_assert(sizeof(GlobalMapPlace) == 0x18);

class CGlobalMap : public CUiWindow {
public:
    // 0062D350 (slot 25). Builds the map of the local player's allod: the island model, the
    // zones, the quests the player holds ("q.<zone>.<quest>" = 1) and the links to zones the
    // player may enter ("z.<zone>" = 2), the clock text, the camp and turn buttons, the weather.
    void Create();

    std::uint8_t unknown_148[0x164 - 0x148];
    int zoneCount_;                  // +0x164
    int questCount_;                 // +0x168
    int linkCount_;                  // +0x16C
    int markerCount_;                // +0x170 zones + quests + links
    int unknown_174;                 // +0x174
    int unknown_178;                 // +0x178
    char* allodTitle_;               // +0x17C CString
    float position_[3];              // +0x180 the current zone on the map model
    MfcArray<GlobalMapZone> zones_;  // +0x18C
    MfcArray<GlobalMapPlace> links_; // +0x1A0
    MfcArray<GlobalMapPlace> quests_;   // +0x1B4
    std::uint8_t unknown_1C8[0x1EC - 0x1C8];
    float turn_[4];                  // +0x1EC the model's orientation (a quaternion)
    int unknown_1FC;                 // +0x1FC
    float depth_;                    // +0x200 10
    float centreX_;                  // +0x204 400 (layout)
    float centreY_;                  // +0x208 280
    int unknown_20C;                 // +0x20C 1
    CUiSurface* backdrop_;           // +0x210 a full-screen surface (not in the surface list)
    void* weather_;                  // +0x214 (vtable 007420D4, 0x34 bytes)
    int weatherTable_[18];           // +0x218 .. +0x25C
    int weatherIndex_;               // +0x260
    bool unknown_264;                // +0x264
    std::uint8_t unknown_265[0x278 - 0x265];
    bool gipat_;                     // +0x278 the allod is gipat
    std::uint8_t unknown_279[3];
    int noCamp_;                     // +0x27C the player's "i.nocamp" (1: no camp button)
    float fog_[3];                   // +0x280 by allod
    bool autosave_;                  // +0x28C save "saveb" once built (arrived from a zone)
    std::uint8_t unknown_28D[3];
    int windSound_;                  // +0x290
};
static_assert(offsetof(CGlobalMap, zoneCount_) == 0x164);
static_assert(offsetof(CGlobalMap, allodTitle_) == 0x17C);
static_assert(offsetof(CGlobalMap, zones_) == 0x18C);
static_assert(offsetof(CGlobalMap, links_) == 0x1A0);
static_assert(offsetof(CGlobalMap, quests_) == 0x1B4);
static_assert(offsetof(CGlobalMap, turn_) == 0x1EC);
static_assert(offsetof(CGlobalMap, depth_) == 0x200);
static_assert(offsetof(CGlobalMap, backdrop_) == 0x210);
static_assert(offsetof(CGlobalMap, weatherTable_) == 0x218);
static_assert(offsetof(CGlobalMap, weatherIndex_) == 0x260);
static_assert(offsetof(CGlobalMap, gipat_) == 0x278);
static_assert(offsetof(CGlobalMap, autosave_) == 0x28C);
static_assert(sizeof(CGlobalMap) == 0x294);

// --- Camp pages -----------------------------------------------------------------------------
//
// The camp (CCampScreen in ui_screens.h, vtables 007408E8 / 0074091C, 0x260 bytes) with the
// fields its page switch uses (ui_camp.cpp). Its child panels are created by Create (0060C870).
// Pages: 0 equipment, 1 trade, 2 spell constructor, 3 its second page, 4 item constructor, 5 and 6
// its other pages, 7 leave the camp.
class CCampPages : public CUiWindow {
public:
    // 0060E9E0. Shows page `page` (regions 4-11 and Esc call it): hides every page's panels,
    // shows the page's ones, its title and the strip that goes with it; a constructor page the
    // camp does not offer falls back to the current page. Page 7 gives the constructors' unused
    // items back, puts the constructed items into the strips and leaves the camp.
    void ShowPage(int page);

    std::uint8_t unknown_148[0x190 - 0x148];
    int page_;                       // +0x190
    bool fromHeroScreen_;            // +0x194 opened from the multiplayer hero screen (closes with a pending value)
    bool spellConstructor_;          // +0x195 the spell constructor's pages are offered
    bool itemConstructor_;           // +0x196 the item constructor's pages are offered
    std::uint8_t unknown_197[0x1B0 - 0x197];
    int hero_;                       // +0x1B0 the hero shown
    // Item lists (0x70 bytes each, 00661050): the constructors' parts and what they made.
    void* itemParts_[3];             // +0x1B4 (the constructor's +0x3C), +0x1B8, +0x1BC
    void* itemResults_;              // +0x1C0
    void* itemExtra_;                // +0x1C4
    void* spellParts_[3];            // +0x1C8 (the constructor's +0xAC), +0x1CC, +0x1D0
    void* spellResults_;             // +0x1D4
    CUiWindow* statsPanel_;          // +0x1D8 (vtable 00741ABC)
    CUiWindow* helpPanel_;           // +0x1DC (vtable 007404DC; camp help texts)
    CUiItemStrip* itemStrip_;        // +0x1E0
    CUiWindow* equipment_;           // +0x1E4 (vtable 007418E0)
    CUiWindow* equipment2_;          // +0x1E8 (vtable 00741844)
    CUiItemStrip* spellStrip_;       // +0x1EC
    CUiItemStrip* partStrip_;        // +0x1F0
    CUiWindow* trade_;               // +0x1F4 (vtable 007417A8)
    CUiWindow* spellConstructor1_;   // +0x1F8 (vtable 0074170C)
    CUiWindow* spellConstructor2_;   // +0x1FC (vtable 00741670)
    CUiWindow* itemConstructor1_;    // +0x200 (vtable 0074170C)
    CUiWindow* itemConstructor2_;    // +0x204 (vtable 007415D4)
    CUiWindow* itemConstructor3_;    // +0x208 (vtable 00741538)
    std::uint8_t unknown_20C[0x244 - 0x20C];
    char** pageTitles_;              // +0x244 CStringArray data
    std::uint8_t unknown_248[0x254 - 0x248];
    bool noConstructor_;             // +0x254 the player's "i.noconstr" = 1: second pages hidden
    std::uint8_t unknown_255[0x260 - 0x255];
};
static_assert(offsetof(CCampPages, page_) == 0x190);
static_assert(offsetof(CCampPages, hero_) == 0x1B0);
static_assert(offsetof(CCampPages, spellResults_) == 0x1D4);
static_assert(offsetof(CCampPages, statsPanel_) == 0x1D8);
static_assert(offsetof(CCampPages, trade_) == 0x1F4);
static_assert(offsetof(CCampPages, itemConstructor3_) == 0x208);
static_assert(offsetof(CCampPages, pageTitles_) == 0x244);
static_assert(offsetof(CCampPages, noConstructor_) == 0x254);
static_assert(sizeof(CCampPages) == 0x260);
