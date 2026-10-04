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
    // 00636020 (slot 20). Draws the screen: the menu object's frame call, the viewport (with
    // +0x1C1), a mouse move at the current position, clears the depth (and stencil) buffer, then
    // the panels (00600EF0). Rebirth's AddNewConsoleCommands applies "nogui" first. Returns 1.
    BOOL Draw(void* target);

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
    // 0062F050 (slot 24, every frame). While turning, turns the model and moves the markers and
    // their regions with it; bobs the current zone's arrow, animates the layers, refreshes the
    // zone selection when asked, draws the weather into texture 4, sets the cursor.
    void Update();

    std::uint8_t unknown_148[0x164 - 0x148];
    int zoneCount_;                  // +0x164
    int questCount_;                 // +0x168
    int linkCount_;                  // +0x16C
    int markerCount_;                // +0x170 zones + quests + links
    int turning_;                    // +0x174 -1 / +1 while a turn button is held
    float bobTime_;                  // +0x178 seconds, for the arrow's bobbing and the layers' frames
    char* allodTitle_;               // +0x17C CString
    float position_[3];              // +0x180 the current zone on the map model
    MfcArray<GlobalMapZone> zones_;  // +0x18C
    MfcArray<GlobalMapPlace> links_; // +0x1A0
    MfcArray<GlobalMapPlace> quests_;   // +0x1B4
    std::uint8_t unknown_1C8[0x1EC - 0x1C8];
    float turn_[4];                  // +0x1EC the model's orientation (a quaternion)
    float angle_;                    // +0x1FC the model's turn about z (mod 007C8020)
    float depth_;                    // +0x200 10
    float centreX_;                  // +0x204 400 (layout)
    float centreY_;                  // +0x208 280
    int selectPending_;              // +0x20C 1: the zone selection is refreshed (00630670) on the next update
    CUiSurface* backdrop_;           // +0x210 a full-screen surface (not in the surface list)
    void* weather_;                  // +0x214 (vtable 007420D4, 0x34 bytes)
    int weatherTable_[18];           // +0x218 .. +0x25C
    int weatherIndex_;               // +0x260
    bool dragging_;                  // +0x264 the map is turned with the mouse
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
    // 006100E0 (virtual slot 43). Whether `item` may go where the strip with action `mode` puts
    // it on the current page: weapons and armour on the equipment page, spells the party can cast,
    // the parts and spells each constructor accepts (skill and mana of the best hero, at most
    // eight modifiers, matching blueprints). Returns AL.
    bool CanEquip(std::uint8_t* item, int mode);
    // 00611930 (every frame from the camp's draw, and on the hero arrows). Works out the page's
    // action (repair all, level up, trade, construct, take apart, repair), its cost against the
    // money (or experience on the trade page) and draws them on surface 0; shades the two action
    // buttons by whether they can be pressed.
    void UpdateAction();
    // 00612290 / 00612600. Runs the spell (item) constructor over the page's slots and keeps
    // what it makes in spellMade_ (itemMade_, with a reference); true when it can be made (the
    // party's skill and mana suffice for the spell). Return AL.
    bool CanMakeSpell();
    bool CanMakeItem();
    // 0060FAB0 (virtual slot 54). The price of `item` for deal `kind` (8: repair): the item's base
    // price (its slot 16) times the camp's multiplier for the kind (+0x21C floats); weapons and
    // armour also lose the repair cost of their wear (1 - durability / maximum). A repair costs at
    // least 1, or 0 for an unworn item. Rebirth's FixRepairPrice clamps the wear to [0, 1] and adds
    // the repair cost instead of subtracting it when an item is taken apart (kind 7).
    int Price(std::uint8_t* item, int kind);
    // 0060F050 (slot 20). Finds what is under the mouse (a page button's help, an item of a
    // panel or a strip, the trade's or the hero's statistics) for the description panel, updates
    // the action, draws the children and, on the equipment page, the hero. Returns 1.
    BOOL Draw(int pass);
    // 0060E6B0 (slot 26, leaving the camp). Clears the server flag (00578D10) and render state 2;
    // hands the local player's unit records to 005789E0 with +0x1C raised by 100000000 for the
    // call; frees the heroes array, releases the constructed item and spell, empties both
    // description panels and deletes the constructors' part lists except the first of each.
    void Close();

    std::uint8_t unknown_148[0x164 - 0x148];
    CUiWindow* descPanel_;           // +0x164 the description panel (vtable 007404DC)
    std::uint8_t* hoveredItem_;      // +0x168 the item under the mouse (set by Draw)
    std::uint8_t unknown_16C[0x190 - 0x16C];
    int page_;                       // +0x190
    bool fromHeroScreen_;            // +0x194 opened from the multiplayer hero screen (closes with a pending value)
    bool spellConstructor_;          // +0x195 the spell constructor's pages are offered
    bool itemConstructor_;           // +0x196 the item constructor's pages are offered
    std::uint8_t unknown_197[0x1A0 - 0x197];
    std::uint8_t** heroes_;          // +0x1A0 the party's heroes (data of an array)
    std::uint8_t unknown_1A4[0x1B0 - 0x1A4];
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
    int cost_;                       // +0x20C what the action costs (negative: earns)
    bool actionEnabled_;             // +0x210 the action button can be pressed
    bool actionShown_;               // +0x211
    bool deconstructing_;            // +0x212 the constructor takes an item apart
    std::uint8_t unknown_213;
    std::uint8_t* itemMade_;         // +0x214 the item the item constructor would make, or null
    std::uint8_t* spellMade_;        // +0x218 the spell the spell constructor would make, or null
    std::uint8_t unknown_21C[0x244 - 0x21C];
    char** pageTitles_;              // +0x244 CStringArray data
    std::uint8_t unknown_248[0x254 - 0x248];
    bool noConstructor_;             // +0x254 the player's "i.noconstr" = 1: second pages hidden
    std::uint8_t unknown_255[0x258 - 0x255];
    std::uint32_t heroTurn_;         // +0x258 passed to the hero preview (005EC300)
    std::uint8_t unknown_25C[0x260 - 0x25C];
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

// --- Hero statistics panel ----------------------------------------------------------------------
//
// The statistics column shared by the camp and the multiplayer hero screen (vtables 00741ABC /
// 00741A88, 0x17C bytes; ui_hero_panels.cpp). Its owner's slot 51 gives the hero record (stats at
// +0x20), slot 50 the hero's unit, slot 53 (hero screen) the equipment weight.
class CHeroStatsPanel : public CUiWindow {
public:
    // 006233C0. Shows the statistics for page `page` of the owner: 0 the body parts and the
    // weapon, 10 the new hero's points with -/+ buttons, others the plain values; rebuilds the
    // rows' regions (two fixed ones kept) and draws (DrawStats).
    void ShowStats(int page);
    // 00623880. Draws the statistics into surface 0.
    void DrawStats();
    // 00622DB0 (slot 18). Builds the panel: the hero arrows' regions, a surface, the arrow,
    // frame and -/+ button sprites, and the texts (CStringArray at +0x15C).
    void Build();

    std::uint8_t unknown_148[4];
    CUiWindow* owner_;               // +0x14C
    int heroIndex_;                  // +0x150 passed to the owner's slot 50
    char* title_;                    // +0x154 CString
    int page_;                       // +0x158
    std::uint8_t unknown_15C[4];
    char** texts_;                   // +0x160 CStringArray data: weapon labels, the points texts, ...
    std::uint8_t unknown_164[0x170 - 0x164];
    int points_;                     // +0x170 skill points left (page 10)
    int unknown_174_;                // +0x174 -1
    int unknown_178_;                // +0x178 0
};
static_assert(offsetof(CHeroStatsPanel, owner_) == 0x14C);
static_assert(offsetof(CHeroStatsPanel, page_) == 0x158);
static_assert(offsetof(CHeroStatsPanel, texts_) == 0x160);
static_assert(offsetof(CHeroStatsPanel, points_) == 0x170);
static_assert(sizeof(CHeroStatsPanel) == 0x17C);

// --- Hero equipment panel -----------------------------------------------------------------------
//
// The hero's equipment (vtables 007418E0 / 007418AC, 0x158 bytes; ui_hero_panels.cpp): the eight
// quick slots along the top, and in the camp the body with its seven equipment slots.
class CHeroEquipPanel : public CUiWindow {
public:
    // 00622000. Builds the panel: eight slot regions and their frames along the strip, two
    // highlight frames; with `body` (the camp) the panel is 500 high and gets the seven body slot
    // regions and the four body pictures (textures camp1..camp4).
    void Build(bool body);

    int unknown_148;                 // +0x148 0
    std::uint8_t unknown_14C[4];
    bool body_;                      // +0x150
    std::uint8_t unknown_151[3];
    int selected_;                   // +0x154 -1
};
static_assert(offsetof(CHeroEquipPanel, body_) == 0x150);
static_assert(sizeof(CHeroEquipPanel) == 0x158);

// --- Camp trade grid ----------------------------------------------------------------------------
//
// The 4 x 4 grid of item cells on the constructors' first pages (vtables 0074170C / 007416D8,
// 0x164 bytes; ui_hero_panels.cpp).
class CTradeGrid : public CUiWindow {
public:
    // 006270A0. Builds the grid over 200..600 x 100..500: the cell pictures (textures trade1..4 by
    // quarter), their frames, a region per cell, and the surface. `spells` chooses the price kinds
    // (0/1 for the spell constructor, 4/5 for the item one).
    void Build(void* bought, void* sold, char spells, char flag);

    std::uint8_t unknown_148[0x150 - 0x148];
    void* bought_;                   // +0x150 an item list (+0x40 of a 00661050 list)
    void* sold_;                     // +0x154
    int buyKind_;                    // +0x158
    int sellKind_;                   // +0x15C
    char flag_;                      // +0x160
    std::uint8_t unknown_161[3];
};
static_assert(offsetof(CTradeGrid, bought_) == 0x150);
static_assert(sizeof(CTradeGrid) == 0x164);

// The repair page's grid (vtables 00741538 / 00741504, 0x154 bytes): the same cells as the trade
// grid with textures repair1..4.
class CRepairGrid : public CUiWindow {
public:
    // 006279E0. Builds the grid for the item list `items`.
    void Build(void* items);

    int unknown_148;                 // +0x148 8
    std::uint8_t unknown_14C[4];
    void* items_;                    // +0x150
};
static_assert(offsetof(CRepairGrid, items_) == 0x150);
static_assert(sizeof(CRepairGrid) == 0x154);

// The quick item strip of the hero panels (vtables 00741844 / 00741810, 0x158 bytes): eight
// cells along the top (camp) or the bottom of the screen.
class CHeroQuickStrip : public CUiWindow {
public:
    // 00622A80. Builds the strip at y 0 (`top`) or 500: eight regions, a hidden highlight frame
    // and the cells.
    void Build(bool top);

    int unknown_148;                 // +0x148 0
    std::uint8_t unknown_14C[4];
    int y_;                          // +0x150
    int selected_;                   // +0x154 -1
};
static_assert(offsetof(CHeroQuickStrip, y_) == 0x150);
static_assert(sizeof(CHeroQuickStrip) == 0x158);

// The constructors' second pages: the spell maker (vtables 00741670 / 0074163C, 0x16C bytes) and the
// item maker (007415D4 / 007415A0, 0x158 bytes): the slots of what is being made beside a 2 x 4 grid.
class CConstructorPage : public CUiWindow {
public:
    // 006280C0. The spell maker: the container and spell slots, the grid, the cost line's regions,
    // the four pictures (constrspell1..4), a highlight frame, the cost surface and its texts.
    void BuildSpellMaker(void* slots);
    // 00628CC0. The item maker: the blueprint and two part slots, the grid, the pictures
    // (constritem1..4) and a highlight frame.
    void BuildItemMaker(void* slots);

    int kind_;                       // +0x148 6 spell maker, 7 item maker
    std::uint8_t unknown_14C[4];
    void* slots_;                    // +0x150
    int selected_;                   // +0x154 0
};
static_assert(offsetof(CConstructorPage, slots_) == 0x150);
