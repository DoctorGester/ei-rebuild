#pragma once

// The remaining interface screens of 00600000-00660000 (see docs/notes/ui.md, "Camp, global map
// and battle screen setup"): the battle screen's Create, the global map, the camp's pages and
// the hero panels, the credits. Names provisional. The classes here describe only the fields
// the rewritten code uses; the objects are created by original code (sizes asserted where known).

#include "object_tables.h"
#include "ui.h"
#include "ui_screens.h"

#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

// (UiDwordArray, the MFC CArray<DWORD>-like array, is in ui.h.)

// --- Battle screen --------------------------------------------------------------------------
//
// The battle screen's "pers" texts (an STLport map<int, CString> at +0x1F0): a person's text by
// its text key id. The header node's parent is the tree's root.
struct PersonTextNode {
    int colour;
    PersonTextNode* parent;
    PersonTextNode* left;
    PersonTextNode* right;
    int key;                         // +0x10
    mfc::CString text;               // +0x14
};
struct PersonTextMap {
    PersonTextNode* header;
    // find(key) as the original inlines it (ui_battle_mouse.cpp): the lower bound of `key`, or
    // the header when that is the header or holds a greater key.
    const PersonTextNode* Find(int key) const;
};
static_assert(offsetof(PersonTextNode, text) == 0x14);

// The pixels the mouse must move with the button down on the view before the battle screen
// starts a selection box (an interface option; 0 or less: never).
inline int BoxSelectThreshold() { return *reinterpret_cast<const int*>(0x0079BDB0); }

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
    // 00638280 (slot 24, every frame; ui_battle_update.cpp). When the one selected party unit asks
    // for it (its slot 55), empties the weapon, quick item, spell and action panels and shows the
    // unit in the unit panel; a multiplayer client rebuilds the party from the session's players
    // (sorted ids; the camera's player first); the children's update, the next queued message;
    // the "leave_zone" box when a saved zone exit is pending (+0x18C); with a selection box
    // (+0x1C1) the party units inside +0x1CC are marked (flag 0x100) and the others unmarked; the
    // quest sound asked for in +0x1FC.
    void Update();
    // 00638B30 (input side slot 3, RET 0xC; ui_battle_update.cpp; `this` the window). The left
    // button released: nothing while a portrait click picks a player (+0x22C); without a press on
    // the view (+0x1C0) the container's handler (006011F0); a press without a drag (+0x1C1), or on
    // a network client, is a click on the unit under the point (00565E40, units with flag
    // 0x400000 only) through SelectUnit (00638EF0); a drag selects the party units inside the box
    // (+0x1CC): the world's selection cleared (00566100 with 0), the box marks and portraits'
    // markers reset, each unit inside controlled (00566030) with its portrait marked and its face
    // turned (about -x by 007C8144 / 7), then slot 32 with the first one's id; then 00637DC0.
    void OnLButtonUp(int buttons, int x, int y);
    // 00639D60 (input side slot 4, RET 0xC; ui_battle_mouse.cpp; `this` the window). The mouse
    // moved (also every frame from Draw, at the current position): while a portrait click picks a
    // player (+0x22C) the cursor shape 0xD and the portraits' own mouse move; otherwise shape 0,
    // then with a press on the view (+0x1C0), a positive BoxSelectThreshold and not a network
    // client: a move farther than the threshold from the press point (+0x1C4) in x or y starts
    // the selection box (+0x1C1), which empties the unit panel's hover unit and spans the press
    // point and (x, y) (+0x1CC); before that the unit under the point is hovered at once.
    // Without a press the container's handler (006012D0) first, and when no panel took the move
    // the unit under the point (00565E40, any flags) goes to HoverUnit (00639EF0).
    void OnMouseMove(int buttons, int x, int y);
    // 0063B920 (RET 8; ui_battle_mouse.cpp). The name of `unit` into `out`: its GetName (slot 7)
    // when not empty; for a unit whose id is a text key, its "pers" text (+0x1F0) or empty;
    // otherwise the first line of the text "unit " + the prototype's name with spaces as '_' (or
    // empty without such a text). The unit panel, the hover, the selection and speech lines use it.
    void UnitName(CUnit* unit, mfc::CString* out);
    // 006369D0 (slot 33, RET 8; ui_battle_update.cpp). A "q." player variable changed while the
    // screen is on top (the quest log 005F7FC0 passes it on). `name` split at '.': three parts
    // (q.<zone>.<quest>) a quest line, "combat_get_quest" / "_complete_" (value 2) / "_failed_"
    // (3), then the quest's title (text "quest <quest>"); four parts (a sub-objective) the same
    // with "_subobj" and the "#subobj <n>" line, skipped on a single player whose variable
    // "<a>.<b>.<c>" is 0. The line (any part count) goes to the top panel's text (+0x164) and,
    // unless +0x160, the message log; the clock pulses, +0x1FC becomes at least the value (1 for
    // a failure, 3 for a completed quest).
    void OnQuestVariable(const char* name, float value);
    // 00637DC0 (ui_battle_update.cpp; called when a unit joins (slot 29) or leaves (slot 30) the
    // party, after items are picked up (slot 28), by the member toggle 006379E0, a box drag and a
    // network client's party change). The selection changed: with exactly one living party unit
    // selected the weapon bar, quick items, spell and action columns show it, otherwise they are
    // emptied (the action column keeps its extra button while anyone is selected); the unit panel
    // (+0x14C, +0x15C / +0x154 from the prototype's +0x20 / +0x24, 0063B920) shows the first
    // selected unit (null with nobody selected) when it changed; the pending action (+0x17C 0,
    // +0x180 / +0x184 -1) and every panel highlight are cleared.
    void RefreshSelection();
    // 006379E0 (RET 4; ui_battle_commands.cpp). Party member `index` clicked (its portrait or
    // body, SelectUnit 00638EF0; the member keys of 0063A560): nothing on a network client or
    // for an index past the party. The selection is the members' flag 0x10 (as 0x10 or 0); with
    // the interface's +0x7D the member flips (to 1 / 0), else it alone is chosen (1). The world's
    // selection is cleared (00566100 with 0); each portrait's marker (+0x220) gets the chosen
    // value and its face's rest orientation the turn about -x by 007C8144 / 7 (or the identity);
    // each chosen member is controlled (00566030) and the clicked one gets slot 32 (id, 0, 0). In
    // single player (007B0098 1) the local player's unit records (+0x778) follow flag 0x10; then
    // 00637DC0.
    void ToggleMember(int index);
    // 00637190 (RET 8). The selected members attack `target`: the order kind is the first of the
    // six modifier flags +0x18D.. that is set (6 for none); each selected member's portrait nods
    // after a random delay and, when the target is more than five levels above it, the member
    // speaks (slot 32 with event 10); the local view gets the order (0066A880) with the low byte
    // of `ctrl` (the interface's +0x7F).
    void AttackUnit(CUnit* target, int ctrl);
    // 006373A0 (RET 8). The same for a dead `target` (0066AB80; no kind, no levels).
    void OrderDeadUnit(CUnit* target, int ctrl);
    // 00637500 (RET 0xC; ui_battle_commands.cpp). The pending spell (+0x17C 1) or quick item (3) in
    // slot +0x180 used on `target` by each selected member: `quick` 0 a spell (the local view's
    // 0066AC70 (slot, member, target, low byte of `ctrl`), then CUnit::CanCastSpell), else a quick
    // item (0066AD40, the quick item panel shows the member (0063D2B0), CUnit::CanUseQuickItem);
    // when the check answers 0 the member's portrait nods after a random delay. Then the pending
    // action is dropped (+0x17C 0, +0x180 / +0x184 -1) and the quick item, spell and action
    // panels' highlights cleared.
    void UseOnUnit(CUnit* target, char quick, int ctrl);
    // 00637750 (RET 0x14; ui_battle_commands.cpp). The same at the point (x, y, z) (0066ACD0 /
    // 0066ADD0 with the point and 0). For the member that is `unit` itself the point first moves
    // 0.1 (0073B850) behind it (against its rotated Y axis), for it and the members after it.
    void UseAtPoint(float x, float y, float z, CUnit* unit, char quick);
    // 006366F0 (slot 32, RET 0xC). The unit with `id` (the object manager 007AF5F8) speaks: a
    // phrase picked by 006D8430 (or, for events 0x2A / 0x2B, 006D8A40 with `extra`); when its
    // "say <name>" text exists, the unit's name line (0063B920) + ":\n" and the text (to a '#',
    // CR / LF trimmed, + "\n") are appended to the top panel's text and, unless its +0x160, the
    // message log. Then event 0x30 shows that member's timed face (+0x48 1, +0x4C 0), events
    // 0xB..0x15 make its portrait shake (kind 2) after a random delay.
    void OnUnitSpeech(int id, unsigned event, char* extra);

    // The client's party ids (STLport vector<int>s): the ids of players with a unit, the camera's
    // player's id, the ids of the others. Compared and copied as a whole (006381E0, 0064AF30).
    struct IdVector {
        int* begin;
        int* end;
        int* capacity;
    };
    struct PartyIds {
        IdVector withUnit;
        int camera;
        IdVector without;
    };

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
    bool unknown_1C0;                // +0x1C0 the left button went down on the view (00638A40)
    bool unknown_1C1;                // +0x1C1 a selection box is being dragged
    std::uint8_t unknown_1C2[0x1C4 - 0x1C2];
    int pressX_;                     // +0x1C4 where the button went down on the view (screen)
    int pressY_;                     // +0x1C8
    RECT selectionBox_;              // +0x1CC the box (passed to 005656A0)
    UiDwordArray heroes_;            // +0x1DC ids of the heroes shown (saved with the game)
    PersonTextMap personTexts_;      // +0x1F0 the "pers" texts by person
    std::uint8_t unknown_1F4[0x1FC - 0x1F4];
    int unknown_1FC;                 // +0x1FC 0; the quest sound to play (1 quest, 2 part done, 3 done)
    bool hasHero_;                   // +0x200 a hero id was added (new game)
    std::uint8_t unknown_201[3];
    // +0x204 the client's party ids last built (+0x210 = unknown_210 is its camera id, -1).
    IdVector partyWithUnit_;         // +0x204
    int unknown_210;                 // +0x210 -1
    IdVector partyWithout_;          // +0x214
    std::uint8_t unknown_220[0x22C - 0x220];
    int portraitMode_;               // +0x22C 0
};
static_assert(offsetof(CBattleScreenSetup, pressX_) == 0x1C4);
static_assert(offsetof(CBattleScreenSetup, selectionBox_) == 0x1CC);
static_assert(offsetof(CBattleScreenSetup, partyWithUnit_) == 0x204);
static_assert(offsetof(CBattleScreenSetup, partyWithout_) == 0x214);
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
    // 005F67A0 (RET 4). Constructor (inlined into nothing; called by CreateScreen 4): the
    // container base, empty strings and arrays (vtables 00740BFC / 00740BF0 / 00740BE4), the two
    // STLport containers at +0x1D0 and +0x1E0, the identity turn; returns this.
    CGlobalMap* Construct(CInterface3D* ui);

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

// 005F6920 (__fastcall on the map). The destructor body: its strings, the tree at +0x1E0, the
// vector at +0x1C8, the three arrays (entries destroyed), then the container base
// (ui_global_map.cpp).
void __fastcall GlobalMapDestroy(CGlobalMap* self);

// 0060E3E0 (__fastcall on the panel). The trade panel's destructor body (vtable 007417A8):
// its CStringArray (+0x1A4), the vectors at +0x190 (0x3C-byte entries) and +0x180, +0x174, +0x168,
// +0x15C (0x18-byte entries), then the window base (ui_camp.cpp).
void __fastcall TradePanelDestroy(CUiWindow* self);

// --- Camp trade panel (the level-up page) ------------------------------------------------------
//
// The panel that sells skills for experience (vtable 007417A8; the camp's +0x1F4;
// ui_trade_panel.cpp): the hero's six magic schools and science with
// their levels, the perks the hero holds and the perks the hero may learn, each with its price.

// A row of the panel's lists (0x18 bytes, STLport vectors at +0x15C .. +0x180).
struct TradeRow {
    char* name;                      // +0x00 CString shown text
    char* key;                       // +0x04 CString the perk's record name (perk rows)
    char* nextKey;                   // +0x08 CString the next level's record name (held perks below 3)
    int price;                       // +0x0C experience
    int level;                       // +0x10
    bool learnable;                  // +0x14 set on the learnable list (not written in the others)
    std::uint8_t unknown_15[3];
};
static_assert(sizeof(TradeRow) == 0x18);

// STLport vector<TradeRow>.
struct TradeRows {
    TradeRow* begin;
    TradeRow* end;
    TradeRow* capacity;
    int Count() const { return static_cast<int>(reinterpret_cast<const char*>(end) - reinterpret_cast<const char*>(begin)) / 0x18; }
};

// A party member's statistics as the trade panel keeps them (0x3C bytes, STLport vector at
// CTradePanel +0x190; filled by 00626F10 from the local player's unit record `index`).
struct TradeRecord {
    std::uint8_t science;            // +0x00 stats +0x6C8
    std::uint8_t schools[6];         // +0x01 stats +0x6CB..
    std::uint8_t perks[36];          // +0x07 stats +0x6D3..
    std::uint8_t unknown_2B;         // +0x2B not written
    int unknown_2C;                  // +0x2C stats +0x04
    int unknown_30;                  // +0x30 stats +0x08
    int unknown_34;                  // +0x34 stats +0x700
    std::uint8_t unknown_38;         // +0x38 0
    std::uint8_t unknown_39[3];      // not written
};
static_assert(sizeof(TradeRecord) == 0x3C);
struct TradeRecords {
    TradeRecord* begin;
    TradeRecord* end;
    TradeRecord* capacity;
};

class CTradePanel : public CUiWindow {
public:
    // 00624F10 (__fastcall; window slot 18, called by the camp's and the hero screen's Create).
    // Lays the panel out at (200, 100, 600, 500): a surface, the regions (the four scroll arrows,
    // the title rows, 23 learnable rows from (415, 135), 10 held rows from (215, 330)), the arrows'
    // sprites, the three titles ("skills", "perks", "perks_av"), and one statistics record per
    // party member (00626F10 with +0x150 the member's index); +0x19C = -1, +0x1A0 = 0.
    void Build();
    // 00625880 (__fastcall). Rebuilds the lists for the owner's hero (its slot 51): the hero's
    // statistics are reset (00524CD0) and refreshed (owner slot 47); the schools (00793BD4 names,
    // levels at stats +0x6CB, science +0x6C8, prices 00527FF0), the held perks (a level byte per
    // group of three skill records at stats +0x6D3) and every first-level perk the hero may learn
    // (005260A0), named by the texts "perk <name>"; the experience left; then the scroll positions
    // are clamped (00626D70) and the lists drawn.
    void BuildLists();
    // 006261A0 (__fastcall). Draws the lists into surface 0: the three titles, the schools (the
    // third one's row left empty) and science, ten held perks from perkTop_, 23 learnable perks
    // from learnableTop_, each price in grey when it is over the experience, with a coin sprite
    // per price (the scene objects past the first four are dropped first).
    void DrawLists();

    std::uint8_t unknown_148[4];
    CUiWindow* owner_;               // +0x14C the camp or the hero screen (slot 51: the hero record)
    int recordIndex_;                // +0x150 the record 00626F10 fills
    int learnableTop_;               // +0x154 first learnable row shown
    int perkTop_;                    // +0x158 first held perk shown
    TradeRows learnable_;            // +0x15C
    TradeRows perks_;                // +0x168
    TradeRows schools_;              // +0x174 (six)
    TradeRows science_;              // +0x180 (one)
    int experience_;                 // +0x18C experience the hero can spend
    TradeRecords records_;           // +0x190 one per party member
    int unknown_19C;                 // +0x19C -1 after Build
    int unknown_1A0;                 // +0x1A0 0 after Build
    void* textsVtable_;              // +0x1A4 CStringArray: the titles
    char** texts_;                   // +0x1A8
};
static_assert(offsetof(CTradePanel, owner_) == 0x14C);
static_assert(offsetof(CTradePanel, learnable_) == 0x15C);
static_assert(offsetof(CTradePanel, science_) == 0x180);
static_assert(offsetof(CTradePanel, experience_) == 0x18C);
static_assert(offsetof(CTradePanel, texts_) == 0x1A8);
static_assert(offsetof(CTradePanel, records_) == 0x190);
static_assert(offsetof(CTradePanel, unknown_1A0) == 0x1A0);

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
    // 0060FE40 (RET 4; from 0060FCA0 and 00610AD0). Takes `item` (a weapon 0x3004, armour 0x3005,
    // 0x3006 or 0x3008) off the current hero (slot 51) into the player's items (00661DB0, whose
    // result it returns; 0 for other kinds): a weapon also moves the hero's chosen-weapon index
    // (+0x774) back and redraws (slot 46); armour also comes off the hero's unit and the stats'
    // outer layers are rebuilt. The stats panel is redrawn.
    int TakeOff(std::uint8_t* item);
    // 0060FCA0 (RET 4). Puts `item` on the current hero: a weapon (at most 4; the first becomes
    // the chosen one; slot 46 redraws), armour (each piece of the same body part taken off first
    // through TakeOff, then worn by the hero's unit and the stats' outer layers rebuilt), a 0x3006
    // item (at most 4) or a 0x3008 item (at most 8); then the player's items lose it (00661F30)
    // and the stats panel is redrawn. Other kinds and full slots do nothing.
    void PutOn(std::uint8_t* item);
    // 00610920 (RET 4; MoveItem's mode 1, a click on an item strip). Pages 0 / 1 put the item on
    // the hero (PutOn); the other pages move it from the player's items into the page's slots
    // (spellParts_[2], spellResults_, itemParts_[2], itemResults_ (with Shift and a kind-one item
    // the whole stack while slot 43 accepts it with mode 1), itemExtra_). Sound put_on.
    void PutOnFromStrip(std::uint8_t* item);
    // 00610AD0 (virtual slot 42, RET 8; what an item strip or a panel calls with its action mode).
    // Refused by CanEquip (slot 43): the cancel sound. 0 takes the item off the hero into the item
    // strip, 1 PutOnFromStrip, 2 / 3 into the spell / item constructor's slots (put_on), 4 / 5 out
    // of a constructor's trade lists, 6 / 7 out of the makers' slots (a modifier, a container or a
    // kind-one item alone, with Shift all kind-one items and then the first container; anything
    // else empties them), 8 out of itemExtra_; made items (record +0x10) go back to the
    // constructor's parts and strip, the others to the player and the item strip (put_off).
    void MoveItem(std::uint8_t* item, int mode);
    // 0060F9E0 (slot 46). Re-arms the current hero's unit: its figure's weapon reset (-1), the
    // four weapon slots cleared (005310D0), the record's weapons given (00531070), 00531110, the
    // chosen weapon shown (figure slot 25 with +0x774, slot 30); then slot 47.
    void RefreshWeapons();
    // 0060F980 (slot 47). The current hero's unit (if any) takes the record's statistics
    // (00532A20 with record + 0x20) and its figure is updated (slot 31); the stats panel redraws.
    void RefreshStats();
    // 0060F6B0 (slot 24). The children's update, then the hero preview turns while a turn button
    // is held (+0x25C 1 / 2): heroTurn_ +- twice the frame time, modulo 007C7EE8.
    void Update();
    // 0060F710 / 0060F7A0 (slots 48 / 49). The next / previous hero of the party (wrapping; the
    // first record whose prototype is of class 0x32; fatal after a full round without one); a
    // different hero is shown (0060F830) and re-armed (slot 46).
    void NextHero();
    void PreviousHero();
    // 0060F830. The current hero's name for the panels: the unit's own name, else the text of
    // "pers <record name>" when the text table has it; to the stats panel (006232D0) and the trade
    // panel (00625860).
    void ShowHero();
    // 005F6240 (RET 8; CreateScreen 5). Constructor: the container base, both item caches (limit
    // 50; the item screens' vtables 00740A2C / 007409F8 in between), fromHeroScreen_ from the
    // flag's low byte, the heroes array (vtable 007402AC), the page titles' CStringArray; the
    // camp's vtables 0074091C / 007408E8. Returns this.
    CCampPages* Construct(CInterface3D* ui, int fromHeroScreen);

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
