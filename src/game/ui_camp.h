#pragma once

// The remaining interface screens (see docs/notes/ui.md, "Camp, global map, hero
// screens"): the battle screen's Create, the global map, the camp's pages and
// the hero panels, the credits. Names provisional. The classes here describe only the fields
// the code uses; the objects are created by original code (sizes asserted where known).

#include "object_tables.h"
#include "options.h"
#include "ui.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/vecmath.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

class CItem;
class CParty;
class CUnit;
class CConstructorPage;
class CGlobalMapClouds;   // global_map_clouds.h
class CHeroEquipPanel;
class CHeroQuickStrip;
class CHeroStatsPanel;
class CRepairGrid;
class CSpellMakerPage;
class CTradeGrid;

// The hero panel's seven body slots: their centres (800 x 600 layout; the panel's frames and its depth clear).
inline constexpr int kBodySlotX[7] = /*00741EC4*/ {250, 250, 250, 550, 550, 550, 550};
inline constexpr int kBodySlotY[7] = /*00741EE0*/ {150, 300, 450, 150, 350, 450, 250};
// The global map model's spin wraps at this (a static initialiser: pi + pi).
inline constexpr float kGlobalMapFullTurn = /*007C8020*/ 6.2831855f;

// (UiDwordArray, the MFC CArray<DWORD>-like array, is in ui.h.)

// --- Battle screen (CBattleScreen and PersonTextMap are in ui.h) ---------------------------
// The "pers <name>" texts read in (ui_person_texts.cpp): every text section "pers <name>" gives its
// first line to the key of <name> (inserted, or replaced). The battle screen's setup does it
// inline; the briefing screen.
void LoadPersonTexts(PersonTextMap* map);

// The pixels the mouse must move with the button down on the view before the battle screen
// starts a selection box (an interface option; 0 or less: never).
inline int BoxSelectThreshold() { return options::TheOptions()->cameraFrameSelectionArea_; }

struct ItemHoverInfo;
class ClassTemplate;
struct PartyUnitRecord;

// The statistics' names, also their text keys: the six skill schools followed by "Science"; the
// attributes (index 7 "Encumbrance").
inline const char* const* SkillNames() {
    static constexpr const char* kNames[7] = /*00793BD4*/ {"Melee", "Archery", "Backstab", "Elemental", "Sense",
                                                           "Astral", "Science"};
    return kNames;
}
inline const char* const* AttributeNames() {
    static constexpr const char* kNames[8] = /*00793BF0*/ {"Strength", "Dexterity", "Intelligence", "Health",
                                                           "Mana",     "Actions",   "Experience",   "Encumbrance"};
    return kNames;
}

// --- Global map -----------------------------------------------------------------------------
// The island map shown between zones (CreateScreen id 4, 0x294 bytes).
// Regions: 0 / 1 turn the view, 2 the camp, 3.. the links between zones.

// A zone of the allod on the map (0x10 bytes, CArray at +0x18C).
struct GlobalMapZone {
    int state;                       // +0x00 the player's "z.<zone>" variable, truncated
    int link;                        // +0x04 -1, or the link that leads to it (state 2)
    mfc::CString name;               // +0x08
    mfc::CString title;              // +0x0C
};
static_assert(sizeof(GlobalMapZone) == 0x10 && offsetof(GlobalMapZone, name) == 0x08 && offsetof(GlobalMapZone, title) == 0x0C);
// The zone array's ConstructElements (zero-filled, the two CStrings empty) and
// DestructElements (per zone: title, then name), and the element assignment
// CArray::SetAtGrow does (ui_global_map.cpp).
struct GlobalMapZoneElements {
    static void Construct(GlobalMapZone* zones, int count);
    static void Destruct(GlobalMapZone* zones, int count);
};
struct GlobalMapZoneTable : mfc::CArray<GlobalMapZone, GlobalMapZoneElements> {
    // Zones[index] = zone, growing the array (and constructing the new
    // zones) when `index` is past its end; the strings are assigned as CStrings.
    void SetAtGrowZone(int index, const GlobalMapZone* zone);
};
using GlobalMapZoneArray = GlobalMapZoneTable;

// A quest place or a link (an "edge" zone) on the map (0x18 bytes, CArrays at +0x1A0 / +0x1B4).
struct GlobalMapPlace {
    // (identical copies, the links' and the quests' arrays' one).
    // The element assignment: zone and position copied, the two strings
    // assigned as CStrings. Returns this.
    GlobalMapPlace* Assign(const GlobalMapPlace& from);
    GlobalMapPlace* AssignCopy(const GlobalMapPlace& from);

    int zone;                        // +0x00 index into the zones
    Vector3 position;                // +0x04 on the map model
    mfc::CString name;               // +0x10
    mfc::CString title;              // +0x14
};
static_assert(sizeof(GlobalMapPlace) == 0x18 && offsetof(GlobalMapPlace, position) == 0x04 && offsetof(GlobalMapPlace, name) == 0x10 &&
              offsetof(GlobalMapPlace, title) == 0x14);

// A zone on the way of the global map's way search: the map entry and the exit taken (8 bytes).
struct GlobalMapStep {
    MapRecord* zone;
    int exit;
};

class CGlobalMap : public CUiContainer {
public:
    // The container base, empty strings and
    // arrays, the two STLport containers at +0x1D0 and
    // +0x1E0, the identity turn.
    explicit CGlobalMap(CInterface3D* ui);
    // Its strings, the
    // tree at +0x1E0, the vector at +0x1C8, the three arrays (entries destroyed), then the
    // container's.
    ~CGlobalMap() override;
    // Virtuals of its own.
    BOOL Draw(int pass) override;   // The container's, blended
    void CloseScreen() override { Close(); }                                                 // 26
    void MessageResult(int result) override { OnMessageResult(result); }                    // 27
    void Deactivate() override { OnDeactivate(); }                                           // 1
    int LeftDown(int b, int x, int y) override { return OnLeftDown(b, x, y), 0; }            // 2
    int LeftUp(int b, int x, int y) override { return OnLButtonUp(b, x, y); }                // 3
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }          // 4
    int RightDown(int b, int x, int y) override { return OnRightDown(b, x, y), 0; }          // 5
    int RightUp(int b, int x, int y) override { return OnRightUp(b, x, y), 0; }              // 6
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }              // 9
    // The screen's part of a save game: one zero dword.
    void SaveState() override;
    // Builds the map of the local player's allod: the island model, the
    // zones, the quests the player holds ("q.<zone>.<quest>" = 1) and the links to zones the
    // player may enter ("z.<zone>" = 2), the clock text, the camp and turn buttons, the weather.
    void CreateScreen() override;
    // While turning, turns the model and moves the markers and
    // their regions with it; bobs the current zone's arrow, animates the layers, refreshes the
    // zone selection when asked, draws the weather into texture 4, sets the cursor.
    void Update() override;

    // (`this` is not read). The text "allod <name>",
    // "zone <name>" or "quest <name>" (empty when there is none) constructed into `out`; returns
    // `out` (ui_global_map_places.cpp).
    mfc::CString* AllodTitle(mfc::CString* out, const char* allod);
    mfc::CString* ZoneTitle(mfc::CString* out, const char* zone);
    mfc::CString* QuestTitle(mfc::CString* out, const char* quest);
    // (`this` is not read). Where a zone or quest lies on the map model:
    // the line after "#zone <name>"'s "#position" line, or after "#quest <name>", in map.txt
    // (three numbers, missing ones 0). Returns `out`; not found leaves `out` as it is (the
    // original writes its uninitialised locals there).
    Vector3* ZonePosition(Vector3* out, const char* zone);
    Vector3* QuestPosition(Vector3* out, const char* quest);
    // The lookup behind them (`format` "#zone %s" or "#quest %s"; `positionLine` looks for
    // "#position" after it); false when map.txt has no such entry.
    static bool ReadMapPosition(Vector3* out, const char* format, const char* name, bool positionLine);
    // Shows `index` as the selected zone (`isLink` false) or link (true): each
    // zone's marker gets texture 2 (state 0) or 0 and the tint 1 (state 1) or 0.5 (state 2), the
    // selected one the allod's colour +0x280 (halved for state 2); the links' markers the colour
    // or 1. On gipat a zone and its link are selected together. -1 selects nothing.
    void SelectZone(int index, bool isLink);
    // Applies weather entry +0x260: 0-3 the display's colour +0x14 from table entries
    // 0-3, 4-7 / 8-11 / 12-15 the three layers' plain colours from entries 4-15 (each ARGB from
    // four ints, alpha last), 16 and up the weather object's ramp from entries 16 and 17.
    void SetWeather();
    // Renders the pick picture: the zone
    // markers alone in plain colours 0xFF181818 + 0x101010 * i (flat shading, no dither, the
    // display colour cleared, both surfaces hidden) through a frame step ((0, 0)), copies
    // the screen into the backdrop, then shows everything again but the last two
    // scene objects and the second surface, and selects nothing ((-1, 0)). The click
    // reads the zone back from the backdrop's pixel. Nothing without a backdrop.
    void RefreshSelection();
    // The way search, recursive: for each exit of `from` whose zone exists,
    // a zone named target_ records its arrival exit's time in arrivals_ (the first time, or a
    // smaller one) and, once, searches on beyond it; then, unless the zone is already on the way,
    // the way goes on through it when the player may pass ("z.<zone>" 2 for a game zone, not 1
    // for an edge, not 0 for a brief zone), pathTime_ growing by the exit's "#passtime".
    void FindPaths(MapRecord* from);
    // Input side (ui_global_map_input.cpp; `this` the window). The right button turns the map by
    // dragging: starts the drag (dragging_, turning_ 0, the point
    // in layout coordinates and the angle kept, the mouse motion cleared, the cursor held
    // in place); end it (dragging_
    // cleared, the zone selection refreshed, the motion cleared, the cursor let go and put back at
    // the mouse position (SetCursorPos)).
    void OnRightDown(int buttons, int x, int y);
    void OnRightUp(int buttons, int x, int y);
    void OnDeactivate();
    // (input slot 4; Rebirth-patched; reproducing WideScreen, so hooked in both
    // launches: the drag scaled without the margin, the pick picture's x without it). The mouse moved. While dragging: the drag motion (printed
    // to the stream) turns the map: angle_ += dx / 300 (layout units), taken mod
    // kGlobalMapFullTurn, and turn_ = the tilt (pi * 0.8 about x) times the spin about z; the motion is
    // cleared. Otherwise the zone or link under the mouse is selected (a link's region 3..; or,
    // off every region, the zone read from the backdrop's pixel at half the point, on gipat its
    // link when it has one; nothing while a turn button is held) and its tip shown: the first
    // surface with the title and the titles of the zone's quests, the last two sprites behind
    // it; a zone in state 0 or nothing hides the tip. A new name plays
    // "buttons\globalmap\zone.wav". Regions past the links do nothing.
    void OnMouseMove(int buttons, int x, int y);
    // (input slot 2; Rebirth-patched; ui_global_map_paths.cpp, reproducing
    // WideScreen, so hooked in both launches: the pick picture's x without the margin). The left button pressed (nothing while
    // dragging). Regions 0 / 1, the turn buttons: turning_ -1 / +1 and
    // "buttons\globalmap\move.wav". Region 2, the camp (unless noCamp_ is 1):
    // "buttons\globalmap\transit.wav", the constructor 0 and screen 5 asked for. A link (regions
    // 3..), or on gipat a zone that has one: "buttons\globalmap\ok.wav", the ways to the link's
    // zone searched from the local player's zone and, when there is one, the time of arrival
    // exit 0 passed and the zone asked for at entrance 0. Any other zone under the
    // mouse (read from the backdrop's pixel at half the point; its state not 0): the sound, the
    // search, and the objectives window (built inline) opened over the map with
    // the zone, the arrival times, two buttons and no zone list.
    void OnLeftDown(int buttons, int x, int y);
    // The container's handler first; Esc opens the escape menu (built
    // inline: 0x168 bytes, container constructor, vtables) and marks
    // the key used; an unused key whose action (KeyActions) is '5' quick-saves ("saveq",
    // with 0), '6' quick-loads it.
    void OnKeyDown(int key, int repeat, int flags);
    // A window above closed: no turn, no drag,
    // the zone selection refreshed, and a mouse move (input slot 4) at the mouse position.
    void OnMessageResult(int result);
    // The help topic: "global_map_nocamp" when noCamp_
    // is 1, else "global_map".
    const char* HelpTopic() override;
    // The left button released: a turn
    // by a turn button ends (turning_ 0) and the zone selection is refreshed on the next update.
    int OnLButtonUp(int buttons, int x, int y);
    // Always true
    // neither the map nor the argument (the clicked region's +0x144) is read.
    bool MayTravel(int unused);
    // The map closes: the weather deleted (its slot 2 with 1), the backdrop
    // surface released and deleted, the wind sound stopped.
    void Close();
    // The marker sprites: the scene objects after the arrow, the base and the island (zones,
    // then quests, then links, then the three layers).
    CUiMesh* Marker(int index) const { return SceneMesh(3 + index); }
    // RefreshSelection's helper: scene objects 0..2 and those after the zone markers shown (1) or
    // hidden (0) through visible_.
    void ShowAllButZones(int visible);

    int zoneCount_;                  // +0x164
    int questCount_;                 // +0x168
    int linkCount_;                  // +0x16C
    int markerCount_;                // +0x170 zones + quests + links
    int turning_;                    // +0x174 -1 / +1 while a turn button is held
    float bobTime_;                  // +0x178 seconds, for the arrow's bobbing and the layers' frames
    mfc::CString allodTitle_;        // +0x17C
    Vector3 position_;               // +0x180 the current zone on the map model
    GlobalMapZoneArray zones_;          // +0x18C
    mfc::CArray<GlobalMapPlace> links_; // +0x1A0
    mfc::CArray<GlobalMapPlace> quests_;   // +0x1B4
    // The way search from the current zone to `target_`: the zones on the way so far,
    // their summed "#passtime", and per exit of the target the party may arrive at the shortest
    // time found (-1 when the target is next to the current zone).
    stlp::vector<GlobalMapStep> path_;  // +0x1C8
    float pathTime_;                 // +0x1D4
    mfc::CString target_;            // +0x1D8
    bool found_;                     // +0x1DC a way reached the target
    bool beyondTarget_;              // +0x1DD the search goes on through the target zone itself
    std::uint8_t unused_1DE[2];
    stlp::map<int, float> arrivals_; // +0x1E0 target exit -> time
    Quaternion turn_;                // +0x1EC the model's orientation (a quaternion)
    float angle_;                    // +0x1FC the model's turn about z (mod kGlobalMapFullTurn)
    float depth_;                    // +0x200 10
    float centreX_;                  // +0x204 400 (layout)
    float centreY_;                  // +0x208 280
    int selectPending_;              // +0x20C 1: the zone selection is refreshed on the next update
    CUiSurface* backdrop_;           // +0x210 a full-screen surface (not in the surface list)
    CGlobalMapClouds* weather_;      // +0x214 (global_map_clouds.h)
    int weatherTable_[18];           // +0x218 .. +0x25C
    int weatherIndex_;               // +0x260
    bool dragging_;                  // +0x264 the map is turned with the mouse
    std::uint8_t unused_265[3];
    POINT dragStart_;                // +0x268 where the drag began (layout)
    float dragAngle_;                // +0x270 angle_ when the drag began
    mfc::CString shownName_;         // +0x274 the zone or link whose tip is shown
    bool gipat_;                     // +0x278 the allod is gipat
    std::uint8_t unused_279[3];
    int noCamp_;                     // +0x27C the player's "i.nocamp" (1: no camp button)
    float fog_[3];                   // +0x280 by allod
    bool autosave_;                  // +0x28C save "saveb" once built (arrived from a zone)
    std::uint8_t unused_28D[3];
    int windSound_;                  // +0x290
};
static_assert(offsetof(CGlobalMap, zoneCount_) == 0x164);
static_assert(offsetof(CGlobalMap, allodTitle_) == 0x17C);
static_assert(offsetof(CGlobalMap, zones_) == 0x18C);
static_assert(offsetof(CGlobalMap, links_) == 0x1A0);
static_assert(offsetof(CGlobalMap, quests_) == 0x1B4);
static_assert(offsetof(CGlobalMap, path_) == 0x1C8);
static_assert(offsetof(CGlobalMap, target_) == 0x1D8);
static_assert(offsetof(CGlobalMap, arrivals_) == 0x1E0);
static_assert(offsetof(CGlobalMap, turn_) == 0x1EC);
static_assert(offsetof(CGlobalMap, depth_) == 0x200);
static_assert(offsetof(CGlobalMap, backdrop_) == 0x210);
static_assert(offsetof(CGlobalMap, weatherTable_) == 0x218);
static_assert(offsetof(CGlobalMap, weatherIndex_) == 0x260);
static_assert(offsetof(CGlobalMap, shownName_) == 0x274);
static_assert(offsetof(CGlobalMap, gipat_) == 0x278);
static_assert(offsetof(CGlobalMap, dragStart_) == 0x268);
static_assert(offsetof(CGlobalMap, dragAngle_) == 0x270);
static_assert(offsetof(CGlobalMap, autosave_) == 0x28C);
static_assert(sizeof(CGlobalMap) == 0x294);

// --- Camp trade panel (the level-up page) ------------------------------------------------------
// The panel that sells skills for experience (the camp's +0x1F4):
// the hero's six magic schools and science with
// their levels, the perks the hero holds and the perks the hero may learn, each with its price.

// A row of the panel's lists (0x18 bytes, STLport vectors at +0x15C .. +0x180).
struct TradeRow {
    // Destructor: nextKey, key, name.
    void Destroy();
    // Destroy, then operator delete when bit 0 of `flags` is set. Returns this.
    TradeRow* DeleteThis(int flags);
    // Copy constructor: the texts (shared), then the
    // price, the level and the flag (the padding is not copied). Returns this.
    TradeRow* CopyConstruct(const TradeRow* from);

    mfc::CString name;               // +0x00 shown text
    mfc::CString key;                // +0x04 the perk's record name (perk rows)
    mfc::CString nextKey;            // +0x08 the next level's record name (held perks below 3)
    int price;                       // +0x0C experience
    int level;                       // +0x10
    bool learnable;                  // +0x14 set on the learnable list (not written in the others)
    std::uint8_t unused_15[3];
};
static_assert(sizeof(TradeRow) == 0x18 && offsetof(TradeRow, key) == 0x04 && offsetof(TradeRow, nextKey) == 0x08 && offsetof(TradeRow, price) == 0x0C);

// STLport vector<TradeRow> (its element policy: recordvec::TradeRowElement).
using TradeRows = stlp::vector<TradeRow>;

// A party member's statistics as the trade panel keeps them (0x3C bytes, STLport vector at
// CTradePanel +0x190; filled from the local player's unit record `index`).
struct TradeRecord {
    std::uint8_t science;            // +0x00 stats +0x6C8
    std::uint8_t schools[6];         // +0x01 stats +0x6CB..
    std::uint8_t perks[36];          // +0x07 stats +0x6D3..
    std::uint8_t padAfterPerks;      // +0x2B not written
    float experience;                // +0x2C stats +0x04
    float spentExperience;           // +0x30 stats +0x08
    float pendingExperience;         // +0x34 stats +0x700
    std::uint8_t bought;             // +0x38 0; 1 once something was bought on the panel
    std::uint8_t padAfterBought[3];  // +0x39 not written
};
static_assert(sizeof(TradeRecord) == 0x3C);
using TradeRecords = stlp::vector<TradeRecord>;

class CTradePanel : public CUiItemPanel {
public:
    // The trade panel of `owner` (inlined by the screens' Creates): the vectors empty, the titles' array.
    CTradePanel(CInterface3D* ui, CItemScreen* owner) : CUiItemPanel(ui, owner) { texts_.Construct(); }
    // Its CStringArray (+0x1A4), the vectors at +0x190
    // (0x3C-byte entries) and +0x180, +0x174, +0x168, +0x15C (0x18-byte entries), then the window's.
    ~CTradePanel() override;
    void Update() override { UpdateHeld(); }                                        // 24
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }   // input 2
    int LeftUp(int b, int x, int y) override { return OnLButtonUp(b, x, y), 0; }       // input 3
    // (__fastcall; window slot 18, called by the camp's and the hero screen's Create).
    // Lays the panel out at (200, 100, 600, 500): a surface, the regions (the four scroll arrows,
    // the title rows, 23 learnable rows from (415, 135), 10 held rows from (215, 330)), the arrows'
    // sprites, the three titles ("skills", "perks", "perks_av"), and one statistics record per
    // party member (with +0x150 the member's index); +0x19C = -1, +0x1A0 = 0.
    void Build() override;
    // Rebuilds the lists for the owner's hero (its slot 51): the hero's
    // statistics are reset and refreshed (owner slot 47); the schools (names,
    // levels at stats +0x6CB, science +0x6C8, prices), the held perks (a level byte per
    // group of three skill records at stats +0x6D3) and every first-level perk the hero may learn
    // named by the texts "perk <name>"; the experience left; then the scroll positions
    // are clamped and the lists drawn.
    void BuildLists();
    // Draws the lists into surface 0: the three titles, the schools (the
    // third one's row left empty) and science, ten held perks from perkTop_, 23 learnable perks
    // from learnableTop_, each price in grey when it is over the experience, with a coin sprite
    // per price (the scene objects past the first four are dropped first).
    void DrawLists();
    // A price as text into `out`:
    // "%d" below 1000, "%.1fk" (price * 0.001f) below 10000, "%dk" below a million, "%.1fm"
    // (* 1e-6f) below ten million, "%dm" below 1e9, else "%.1fg" (* 1e-9f); a negative price
    // shows as 0x7FFFFFFF.
    void FormatPrice(int price, mfc::CString* out);
    // What the statistics under the screen point (x, y) are, for the
    // description panel: nothing while the panel is hidden or off its rows; regions 4-9 the
    // schools (kind 1), 10 science (kind 2), 11-33 the learnable perks and 34 up the held ones
    // (kind 3: the perk's record name without its level digits, plus "0"; held ones with their
    // level); the hero's statistics with it.
    void HoverInfo(int x, int y, ItemHoverInfo* info);
    // The records emptied, then one per party member filled from
    // it (SnapshotRecord through its address; recordIndex_ is left at the last).
    void ResetRecords();
    // Record recordIndex_ (when >= 0) from the party member's statistics:
    // science, the six schools, 36 perk levels, the experience values; bought cleared.
    void SnapshotRecord();
    // The reverse: record recordIndex_ (when >= 0) back into the party
    // member's statistics, bought cleared; when the hit point maximum was and is above
    // 1, the slots are healed by what keeps the hit points in proportion.
    void RestoreRecord();
    // With `all`, every bought record is kept
    // (SnapshotRecord, with `keep`) or undone (puts the hero's statistics back), then
    // recordIndex_ is put back; otherwise only the current record when bought, then BuildLists.
    void Settle(int keep, int all);
    // Shows party member `index`: scroll positions 0, then BuildLists.
    void SelectHero(int index);
    // The experience the shown hero can spend, from its record:
    // pending plus earned minus spent, truncated.
    int Experience() const;
    // What the hero (owner's slot 51) has spent since its record was made (the cost of the
    // perks bought on the panel), truncated.
    int PurchaseCost();
    // Keeps perkTop_ in [0, held - 10] and learnableTop_ in
    // [0, learnable - 23], shows the four scroll arrows (scene objects 0-3) only where the list
    // can scroll, then draws the lists.
    void ClampScroll();
    // Regions 0 / 1 scroll the held perks up / down, 2 / 3 the
    // learnable ones; from 4 the "perk" sound, then a purchase the experience covers: 4-9 a
    // school level and 10 science (below 100; then held: heldRegion_, holdTime_ -0.5, the mouse
    // captured), 11-33 a learnable perk, 34 up (and below 0) the next level of a held perk; the
    // spent experience grows by the price, the record is marked bought and the lists rebuilt.
    void OnLButtonDown(int buttons, int x, int y);
    // Releases the mouse unconditionally and ends a
    // hold (heldRegion_ -1, holdTime_ 0).
    void OnLButtonUp(int buttons, int x, int y);
    // While a school or science row is held
    // (heldRegion_ >= 0) the hold time grows by the frame time; from 0.25 s on, every frame buys
    // the row's next level again while its level is below 100 and the experience covers the price
    // (the record is not marked bought), else the mouse is released and the hold ends. The hold
    // time is not reset after a purchase.
    void UpdateHeld();
    // Whether something was
    // bought for the hero shown (its record's bought byte, in AL).
    std::uint8_t Bought() const;
    // The record of the hero shown (recordIndex_).
    const TradeRecord& Record() const { return records_[recordIndex_]; }

    // (CUiItemPanel's owner_ +0x14C: the camp or the hero screen; slot 51 the hero record.)
    int recordIndex_;                // +0x150 the record fills
    int learnableTop_;               // +0x154 first learnable row shown
    int perkTop_;                    // +0x158 first held perk shown
    TradeRows learnable_;            // +0x15C
    TradeRows perks_;                // +0x168
    TradeRows schools_;              // +0x174 (six)
    TradeRows science_;              // +0x180 (one)
    int experience_;                 // +0x18C experience the hero can spend
    TradeRecords records_;           // +0x190 one per party member
    int heldRegion_;                 // +0x19C the school / science row held after a purchase (-1 after Build)
    float holdTime_;                 // +0x1A0 from -0.5 s (0 after Build)
    mfc::CStringArray texts_;        // +0x1A4 the titles
};
static_assert(offsetof(CTradePanel, owner_) == 0x14C);
static_assert(offsetof(CTradePanel, learnable_) == 0x15C);
static_assert(offsetof(CTradePanel, science_) == 0x180);
static_assert(offsetof(CTradePanel, experience_) == 0x18C);
static_assert(offsetof(CTradePanel, texts_) == 0x1A4);
static_assert(sizeof(CTradePanel) == 0x1B8);
static_assert(offsetof(CTradePanel, records_) == 0x190);
static_assert(offsetof(CTradePanel, holdTime_) == 0x1A0);

// --- Camp pages -----------------------------------------------------------------------------
// The camp (vtables, 0x260 bytes; Create, which Rebirth patches): the
// party's equipment, trade, repair and the item and spell constructors, with three item strips;
// also opened by the multiplayer hero screen (region 8) for a hero's inventory (ui_camp.cpp). Its child panels are created by Create.
// Pages: 0 equipment, 1 trade, 2 spell constructor, 3 its second page, 4 item constructor, 5 and 6
// its other pages, 7 leave the camp.
class CCampPages : public CItemScreen {
public:
    // (ui_camp.cpp; CreateScreen 5, and the hero screen's inventory with
    // `fromHeroScreen`): the item screen, both item caches, the heroes array,
    // the page titles' CStringArray.
    CCampPages(CInterface3D* ui, bool fromHeroScreen);
    // The page titles,
    // the heroes array, then the item screen's.
    ~CCampPages() override;
    // Virtuals of its own.
    void CloseScreen() override { Close(); }                                               // 26
    void MessageResult(int result) override { OnMessageResult(result); }                  // 27
    void TakeItem(CItem* item, int mode) override { MoveItem(item, mode); }               // 42
    void TopicClicked(int page, int unused) override { ShowPageTabs(page, unused); }      // 44
    CUnit* HeroUnit(int index) override { return HeroAt(index); }                         // 50
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }     // 2
    int LeftUp(int b, int x, int y) override { return OnLButtonUp(b, x, y), 0; }         // 3
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }          // 9
    int KeyUp(int k, int r, int f) override { return OnKeyUp(k, r, f), 0; }              // 11
    // Over the party's heroes, the best skill in spell school
    // `school` (rounded to nearest) and the most mana units (maximum mana / period, truncated).
    void BestPartySkill(int school, int* skill, int* mana) override;
    // Shows the strip tabs that go with camp page `page` (1-21).
    void ShowPageTabs(int page, int unused);
    // (Rebirth-patched; ui_camp_create.cpp, reproducing WideScreen, so hooked in
    // both launches). BuildPages, then with Rebirth's WideScreen the two curtains over the
    // screen's margins, shown (widescreen::AddCurtains; sprites 10 and 11).
    void CreateScreen() override;
    // Create's body. Builds the camp: the server flag
    // set; the constructor's class template from the player's "constr.current" (none found: back
    // to the world, nothing built)
    // with its flags, price weights and part lists; the player's "i.noconstr" / "i.off..." /
    // "i.on..." switches; texture "campinfo", twelve regions (the two action buttons, the two
    // hero turn areas, the eight page buttons) and ten button sprites; the item screen's base;
    // two surfaces; seven item lists; the panels (description, statistics, help, the item strip,
    // equipment, quick items, trade, and each offered constructor's strip and pages); the party's
    // heroes handed to the client world as units (records of class 0x32, ids raised by 100000000
    // meanwhile), each idling, armed from its record and wearing its armour; the first hero shown
    // the page titles; the first page (4 item constructor, 2 spell constructor, else
    // 0); a constructor's stock made on its first visit (flag38) from the party's lowest and
    // highest skill per spell school; the allod's constructor music; antialiasing (render state 2
    // = 2) when the device sorts independently.
    void BuildPages();
    // Shows page `page` (regions 4-11 and Esc call it): hides every page's panels,
    // shows the page's ones, its title and the strip that goes with it; a constructor page the
    // camp does not offer falls back to the current page. Page 7 gives the constructors' unused
    // items back, puts the constructed items into the strips and leaves the camp.
    void ShowPage(int page);
    // The local player's record of the hero shown
    // (LocalPlayerData +0x30, 0x784 bytes each; not checked).
    PartyUnitRecord* HeroRecord() override;
    // Whether `item` may go where the strip with action `mode` puts
    // it on the current page: weapons and armour on the equipment page, spells the party can cast,
    // the parts and spells each constructor accepts (skill and mana of the best hero, at most
    // eight modifiers, matching blueprints). Returns 0 or 1.
    bool CanEquip(CItem* item, int mode) override;
    // Works out the page's
    // action (repair all, level up, trade, construct, take apart, repair), its cost against the
    // money (or experience on the trade page) and draws them on surface 0; shades the two action
    // buttons by whether they can be pressed.
    void UpdateAction();
    // Runs the spell (item) constructor over the page's slots and keeps
    // what it makes in spellMade_ (itemMade_, with a reference); true when it can be made (the
    // party's skill and mana suffice for the spell). Returns 0 or 1.
    bool CanMakeSpell();
    bool CanMakeItem();
    // The price of `item` for deal `kind` (8: repair): the item's base
    // price (its slot 16) times the camp's multiplier for the kind (+0x21C floats); weapons and
    // armour also lose the repair cost of their wear (1 - durability / maximum). A repair costs at
    // least 1, or 0 for an unworn item. Rebirth's FixRepairPrice clamps the wear to [0, 1] and adds
    // the repair cost instead of subtracting it when an item is taken apart (kind 7).
    int Price(CItem* item, int kind) override;
    // Finds what is under the mouse (a page button's help, an item of a
    // panel or a strip, the trade's or the hero's statistics) for the description panel, updates
    // the action, draws the children and, on the equipment page, the hero. Returns 1.
    BOOL Draw(int pass) override;
    // Clears the server flag and render state 2;
    // hands the local player's unit records with +0x1C raised by 100000000 for the
    // call; frees the heroes array, releases the constructed item and spell, empties both
    // description panels and deletes the constructors' part lists except the first of each.
    void Close();
    // Takes `item` (a weapon 0x3004, armour 0x3005,
    // 0x3006 or 0x3008) off the current hero into the player's items (whose
    // result it returns; 0 for other kinds): a weapon also moves the hero's chosen-weapon index
    // (+0x774) back and redraws; armour also comes off the hero's unit and the stats'
    // outer layers are rebuilt. The stats panel is redrawn.
    CItem* TakeOff(CItem* item);
    // Puts `item` on the current hero: a weapon (at most 4; the first becomes
    // the chosen one; slot 46 redraws), armour (each piece of the same body part taken off first
    // through TakeOff, then worn by the hero's unit and the stats' outer layers rebuilt), a 0x3006
    // item (at most 4) or a 0x3008 item (at most 8); then the player's items lose it
    // and the stats panel is redrawn. Other kinds and full slots do nothing.
    void PutOn(CItem* item);
    // Pages 0 / 1 put the item on
    // the hero (PutOn); the other pages move it from the player's items into the page's slots
    // (spellParts_[2], spellResults_, itemParts_[2], itemResults_ (with Shift and a kind-one item
    // the whole stack while slot 43 accepts it with mode 1), itemExtra_). Sound put_on.
    void PutOnFromStrip(CItem* item);
    // (what an item strip or a panel calls with its action mode).
    // Refused by CanEquip: the cancel sound. 0 takes the item off the hero into the item
    // strip, 1 PutOnFromStrip, 2 / 3 into the spell / item constructor's slots (put_on), 4 / 5 out
    // of a constructor's trade lists, 6 / 7 out of the makers' slots (a modifier, a container or a
    // kind-one item alone, with Shift all kind-one items and then the first container; anything
    // else empties them), 8 out of itemExtra_; made items (record +0x10) go back to the
    // constructor's parts and strip, the others to the player and the item strip (put_off).
    void MoveItem(CItem* item, int mode);
    // Re-arms the current hero's unit: its figure's weapon reset (-1), the
    // four weapon slots cleared, the record's weapons given, the
    // chosen weapon shown (figure slot 25 with +0x774); then slot 47.
    void RefreshWeapons() override;
    // The current hero's unit (if any) takes the record's statistics
    // (with record + 0x20) and its figure is updated; the stats panel redraws.
    void RefreshStats() override;
    // The children's update, then the hero preview turns while a turn button
    // is held (+0x25C 1 / 2): heroTurn_ +- twice the frame time, modulo 2 pi.
    void Update() override;
    // (slots 48 / 49). The next / previous hero of the party (wrapping; the
    // first record whose prototype is of class 0x32; fatal after a full round without one); a
    // different hero is shown and re-armed.
    void NextHero() override;
    void PreviousHero() override;
    // The current hero's name for the panels: the unit's own name, else the text of
    // "pers <record name>" when the text table has it; to the stats panel and the trade
    // panel.
    void ShowHero();
    // Input side (ui_camp_input.cpp; `this` the window): the
    // container's handler first; region 0 / 1 work out the action (UpdateAction) and carry out
    // the first (when actionEnabled_) or second (when actionShown_) button's
    // deal; on the equipment page regions 2 / 3 turn the hero (turning_ 1 / 2, the move sound,
    // the mouse taken); regions 4-11 show page region - 4 unless shown.
    void OnLButtonDown(int buttons, int x, int y);
    // The container's handler, the mouse let go when this took it,
    // turning_ 0.
    void OnLButtonUp(int buttons, int x, int y);
    // Esc shows page 7 (leaves); on a network client Backspace and
    // Enter go to the chat line; those mark the key used, other keys go to the container.
    void OnKeyDown(int key, int repeat, int flags);
    // The container's key-up handler
    // with the same arguments.
    void OnKeyUp(int key, int repeat, int flags);
    // Hero `index` of the party's heroes (heroes_[index], not checked).
    CUnit* HeroAt(int index);
    // The answer 0x10001 takes the shown hero's first weapon back into
    // the party's items (with 0, 1; the hero's entry removed), no weapon
    // chosen (+0x774 -1), the item strip scrolled to it and the hero re-armed.
    void OnMessageResult(int result);
    // The help topic of the page: "camp_weapons", "camp_skills",
    // "camp_spell_trade", "camp_spell_constr", "camp_item_trade", "camp_item_constr",
    // "camp_repair"; none for other pages.
    const char* HelpTopic() override;
    // The buy sound, then by page: 0 (with the
    // item constructor offered and shown) every weapon and armour of the hero repaired; 1 the trade
    // settled (SettleTrade(1, 0); the money is left alone); 2 / 4 the spell / item constructor buys
    // what was put in its third list (each item repaired, its record added to the template's stock
    // lists, or for loot of kind 0 dropped) and the lists go back (the first to the constructor's
    // strip, the second to the player); 3 / 5 the made spell / item replaces what is in the results
    // (or, while deconstructing_, DeconstructSpell / DeconstructItem); 6 the repairs' items repaired
    // and given back. Then the money goes down by cost_.
    void DoAction();
    // The cancel sound, then by page: 1 the trade settled with
    // (0, 0); 2 / 4 the constructor's lists given back (the second to the strip, the third to the
    // player); 3 / 5 every result moved back (made items to the constructor, the rest to the player);
    // 6 the repairs' items given back.
    void UndoAction();
    // The spell container in the results taken apart: a copy kept in
    // spellMade_ while the list is left; a "Prototype" item for its spell and a "Rune"
    // item per modifier go into the results; the copy released.
    bool DeconstructSpell();
    // The item maker's result taken apart: a copy of the first result made
    // (itemMade_ meanwhile) and the result removed; an "Instruction Weapon / Armor / Quick Item" with
    // its prototype index, a "material.<key>" per material its prototype needs (+0x4C), and for an
    // item with a spell a "spell container" (MakeItem 0x3008) holding a clone of it, each added to
    // the results. Returns true.
    bool DeconstructItem();

    // A debug fill of the
    // constructors' first lists: itemParts_[0] cleared (Clear(1, 0)) and given ten of each weapon,
    // armour (names without "prototype") and quick item (names with "wand") instruction, a weapon
    // and armour of each matching material (the material's +0x04 against the record's +0x10,
    // _mbsicmp) and twenty of each "Material.<name>"; spellParts_[0] cleared and given ten of each
    // "Rune" and "Prototype". Items come from the lists' item managers, unchecked.
    void DebugFill() override;

    // The camp's virtual slots the pages call are CItemScreen's Virtual* members: 43 CanEquip (AL),
    // 45 the party's best skill and mana in a school, 46 / 47 RefreshWeapons / RefreshStats, 51 the
    // shown hero's record (HeroRecord), 54 Price (Rebirth-patched).

    int page_;                       // +0x190
    bool fromHeroScreen_;            // +0x194 opened from the multiplayer hero screen (closes with a pending value)
    bool spellConstructor_;          // +0x195 the spell constructor's pages are offered
    bool itemConstructor_;           // +0x196 the item constructor's pages are offered
    std::uint8_t unused_197;
    ClassTemplate* constructor_;     // +0x198 the constructor's class template ("constr.current"), or null
    // +0x19C the party's heroes (CArray): a unit per party record, null for other
    // classes.
    mfc::CArray<CUnit*> heroes_;
    int hero_;                       // +0x1B0 the hero shown (HeroRecord)
    // Item lists (parties, 0x70 bytes each): the constructors' parts and what they made.
    CParty* itemParts_[3];           // +0x1B4 (the constructor's +0x3C), +0x1B8, +0x1BC
    CParty* itemResults_;            // +0x1C0
    CParty* itemExtra_;              // +0x1C4
    CParty* spellParts_[3];          // +0x1C8 (the constructor's +0xAC), +0x1CC, +0x1D0
    CParty* spellResults_;           // +0x1D4
    CHeroStatsPanel* statsPanel_;    // +0x1D8
    CItemInfoPanel* helpPanel_;      // +0x1DC (camp help texts)
    CUiItemStrip* itemStrip_;        // +0x1E0
    CHeroEquipPanel* equipment_;     // +0x1E4
    CHeroQuickStrip* equipment2_;    // +0x1E8
    CUiItemStrip* spellStrip_;       // +0x1EC
    CUiItemStrip* partStrip_;        // +0x1F0
    CTradePanel* trade_;             // +0x1F4
    CTradeGrid* spellConstructor1_;  // +0x1F8
    CSpellMakerPage* spellConstructor2_;   // +0x1FC
    CTradeGrid* itemConstructor1_;   // +0x200
    CConstructorPage* itemConstructor2_;   // +0x204
    CRepairGrid* itemConstructor3_;  // +0x208
    int cost_;                       // +0x20C what the action costs (negative: earns)
    bool actionEnabled_;             // +0x210 the action button can be pressed
    bool actionShown_;               // +0x211
    bool deconstructing_;            // +0x212 the constructor takes an item apart
    std::uint8_t unused_213;
    CItem* itemMade_;                // +0x214 the item the item constructor would make, or null
    CItem* spellMade_;               // +0x218 the spell the spell constructor would make, or null
    float priceWeights_[9];          // +0x21C the constructor's price multipliers (ClassTemplate::weights)
    mfc::CStringArray pageTitles_;   // +0x240 the seven pages' titles and "quit"
    bool noConstructor_;             // +0x254 the player's "i.noconstr" = 1: second pages hidden
    std::uint8_t unused_255[0x258 - 0x255];
    std::uint32_t heroTurn_;         // +0x258 passed to the hero preview
    int turning_;                    // +0x25C 1 / 2 while a turn button is held (Update turns the hero)
};
static_assert(offsetof(CCampPages, page_) == 0x190);
static_assert(offsetof(CCampPages, turning_) == 0x25C);
static_assert(offsetof(CCampPages, hero_) == 0x1B0);
static_assert(offsetof(CCampPages, spellResults_) == 0x1D4);
static_assert(offsetof(CCampPages, statsPanel_) == 0x1D8);
static_assert(offsetof(CCampPages, trade_) == 0x1F4);
static_assert(offsetof(CCampPages, itemConstructor3_) == 0x208);
static_assert(offsetof(CCampPages, pageTitles_) == 0x240);
static_assert(offsetof(CCampPages, heroes_) == 0x19C);
static_assert(offsetof(CCampPages, priceWeights_) == 0x21C);
static_assert(offsetof(CCampPages, noConstructor_) == 0x254);
static_assert(sizeof(CCampPages) == 0x260);

// --- Hero statistics panel ----------------------------------------------------------------------
// The statistics column shared by the camp and the multiplayer hero screen (vtables /
// 0x17C bytes; ui_hero_panels.cpp). Its owner's slot 51 gives the hero record (stats at
// +0x20), slot 50 the hero's unit, slot 53 (hero screen) the equipment weight.
class CHeroStatsPanel : public CUiItemPanel {
public:
    // The statistics column of `owner` (inlined by the screens' Creates): the title and the texts' array.
    CHeroStatsPanel(CInterface3D* ui, CItemScreen* owner) : CUiItemPanel(ui, owner) { texts_.Construct(); }
    // The texts, the title, then the window's.
    ~CHeroStatsPanel() override;
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }   // input 2
    int LeftUp(int b, int x, int y) override { return OnLButtonUp(b, x, y); }          // input 3
    // Shows the statistics for page `page` of the owner: 0 the body parts and the
    // weapon, 10 the new hero's points with -/+ buttons, others the plain values; rebuilds the
    // rows' regions (two fixed ones kept) and draws (DrawStats).
    void ShowStats(int page);
    // Draws the statistics into surface 0.
    void DrawStats();
    // Builds the panel: the hero arrows' regions, a surface, the arrow,
    // frame and -/+ button sprites, and the texts (CStringArray at +0x15C).
    void Build() override;
    // What the statistics under the screen point (x, y) are, for the
    // description panel (the hero's statistics always set): rows 2 up the attributes (pages 1 and
    // 11; page 10 rows 2-4, row 14 the points left, row 5 the height); on page 0 rows 2-5 the body
    // parts' armour, 6 / 7 the shown weapon's attack / defence, 8 the damage, 9 encumbrance, 10
    // the magic skills.
    void HoverInfo(int x, int y, ItemHoverInfo* info);
    // Shows hero `heroIndex` of the owner (and `title` when not null): the
    // hero arrows (scene objects 0, 1) and their regions' tooltips only for a party of more than
    // one, +0x170 = the hero's height * 100 (truncated), then DrawStats (through its address).
    void SetHero(int heroIndex, const char* title);
    // On the new hero's page (10) a -/+
    // button (regions 6-13) steps a point (StepPoint), plays "perk" and refreshes the owner (slot
    // 47); a button that stepped is held (holdTime_ -0.5, the mouse captured). Elsewhere region 0 /
    // 1 play "hero" and show the owner's previous / next hero (slots 49 / 48).
    void OnLButtonDown(int buttons, int x, int y);
    // While a button is held on page 10: after a quarter of a second (holdTime_
    // adding the frame time) it repeats every frame; when it can step no more the hold ends (the
    // capture cleared). The owner is refreshed on every repeat attempt.
    void Update() override;
    // The left button released: the
    // mouse let go (unconditionally) and the hold ended (heldRegion_ -1, holdTime_ 0).
    int OnLButtonUp(int buttons, int x, int y);
    // A -/+ button of page 10: regions 6-8 take a point from strength, dexterity, intelligence
    // (above 15), 9 the height down (points_ >= 1), 10-12 give one (below 35, while the three add
    // up to under 75), 13 the height up (points_ <= 99; height = points_ * 0.01). True when it
    // changed something.
    bool StepPoint(int region);
    // A row on surface 0: the text of "perk <label>" (when
    // the texts have it, else `label` itself) on the left, `value` ("%d", or "%d/%d" with a
    // maximum >= 0) on the right.
    void DrawRow(RECT* rect, const char* label, int value, int maximum);
    // A body part's row: "     " plus the text of "hl_<name>" on the left, its
    // armour ("%.1f") on the right.
    void DrawPart(RECT* rect, const char* name, int part);
    // The armour shown for body part `part`: per damage type the three layers' absorption summed
    // (stored as float), the largest of the first six types (from 0).
    float PartArmour(int part);

    // (CUiItemPanel's owner_ +0x14C.)
    int heroIndex_;                  // +0x150 passed to the owner's slot 50
    mfc::CString title_;             // +0x154
    int page_;                       // +0x158
    mfc::CStringArray texts_;        // +0x15C weapon labels, the points texts, ...
    int points_;                     // +0x170 skill points left (page 10); SetHero puts the height * 100 here
    int heldRegion_;                 // +0x174 the -/+ button held on page 10 (-1 none)
    float holdTime_;                 // +0x178 seconds it has been held (from -0.5)
};
static_assert(offsetof(CHeroStatsPanel, owner_) == 0x14C);
static_assert(offsetof(CHeroStatsPanel, title_) == 0x154);
static_assert(offsetof(CHeroStatsPanel, page_) == 0x158);
static_assert(offsetof(CHeroStatsPanel, texts_) == 0x15C);
static_assert(offsetof(CHeroStatsPanel, points_) == 0x170);
static_assert(sizeof(CHeroStatsPanel) == 0x17C);

// --- Hero equipment panel -----------------------------------------------------------------------
// The hero's equipment (vtables, 0x158 bytes; ui_hero_panels.cpp): the eight
// quick slots along the top, and in the camp the body with its seven equipment slots.
class CHeroEquipPanel : public CUiItemPanel {
public:
    using CUiItemPanel::CUiItemPanel;
    BOOL Draw(int pass) override;   // 20: (ui_container_draws.cpp)
    int RightDown(int b, int x, int y) override { return OnRightDown(b, x, y), 0; }   // input 5
    // Builds the panel: eight slot regions and their frames along the strip, two
    // highlight frames; with `body` (the camp) the panel is 500 high and gets the seven body slot
    // regions and the four body pictures (textures camp1..camp4).
    void Build(bool body);
    // The hero's shown weapon (+0x774) becomes `weapon`'s
    // place in the hero's weapon list (+0x730), or -1.
    void ShowWeapon(CItem* weapon);
    // selected_ becomes `item`'s place in the hero's +0x740 list, or -1.
    void ShowArmour(CItem* item);
    // On the camp's body (body_) while enabled: a right click on
    // one of the first four weapon slots the hero fills plays "change" and shows that weapon; the
    // owner's slot 46 (the weapons refreshed) on any region. The event is not marked used.
    void OnRightDown(int buttons, int x, int y);
    // The item under (x, y) with its help topic: regions 0-3 the
    // weapons (topic 2), 4-7 the +0x740 list from its end (3), on the body regions 8-14 the armour
    // worn at body part region - 8 (topic region - 4); null elsewhere or while disabled (topic 0).
    CItem* ItemAt(int x, int y, int* topic) override;

    bool body_;                      // +0x150 (mode_ 0; owner_ the camp or the hero screen)
    std::uint8_t unused_151[3];
    int selected_;                   // +0x154 -1
};
static_assert(offsetof(CHeroEquipPanel, body_) == 0x150);
static_assert(sizeof(CHeroEquipPanel) == 0x158);

// --- Camp trade grid ----------------------------------------------------------------------------
// The item lists the grids show: a party's or trader's items (CParty +0x40, an engine aligned array).
using UiItemList = lib::AlignedArray<CItem*>;
// The 4 x 4 grid of item cells on the constructors' first pages (vtables,
// 0x164 bytes; ui_hero_panels.cpp).
class CTradeGrid : public CUiItemPanel {
public:
    using CUiItemPanel::CUiItemPanel;
    BOOL Draw(int pass) override { return DrawGrid(pass); }   // 20
    int DrawGrid(int pass);
    // Builds the grid over 200..600 x 100..500: the cell pictures (textures trade1..4 by
    // quarter), their frames, a region per cell, and the surface. `spells` chooses the price kinds
    // (0/1 for the spell constructor, 4/5 for the item one).
    void Build(UiItemList* bought, UiItemList* sold, char spells, char flag);
    // The item under (x, y) with its help topic:
    // regions 0-7 the bought list (topic 0x1C with flag_, else 0x0B for the item constructor,
    // 0x11 for the spell one), 8-15 the sold list (0x1D / 0x0C / 0x12); null elsewhere, past a
    // list's end or while disabled (topic 0 when off the cells or disabled).
    CItem* ItemAt(int x, int y, int* topic) override;

    UiItemList* bought_;             // +0x150 an item list (+0x40 of a list)
    UiItemList* sold_;               // +0x154
    int buyKind_;                    // +0x158
    int sellKind_;                   // +0x15C
    char flag_;                      // +0x160
    std::uint8_t unused_161[3];
};
static_assert(offsetof(CTradeGrid, bought_) == 0x150);
static_assert(sizeof(CTradeGrid) == 0x164);

// The repair page's grid (vtables, 0x154 bytes): the same cells as the trade
// grid with textures repair1..4.
class CRepairGrid : public CUiItemPanel {
public:
    using CUiItemPanel::CUiItemPanel;
    // Builds the grid for the item list `items`.
    void Build(UiItemList* items);
    // The depth buffer cleared to 0 over the screen
    // and to 1 over the grid; each item's model in its cell (depth 2^(3 - row) * 6), its
    // count when more than one and its repair price (owner slot 54, kind 8) on the surface; the
    // window's sprites and models; the depth buffer cleared to 1 again. Returns 1.
    BOOL Draw(int pass) override;
    // The item in cell region (x, y) (topic 0x19
    // on the 16 cells), null past the list's end, elsewhere or while disabled (topic 0).
    CItem* ItemAt(int x, int y, int* topic) override;

    UiItemList* items_;              // +0x150 (mode_ 8, the cells' mode; owner_ the camp)
};
static_assert(offsetof(CRepairGrid, items_) == 0x150);
static_assert(sizeof(CRepairGrid) == 0x154);

// The quick item strip of the hero panels (vtables, 0x158 bytes): eight
// cells along the top (camp) or the bottom of the screen.
class CHeroQuickStrip : public CUiItemPanel {
public:
    using CUiItemPanel::CUiItemPanel;
    // Builds the strip at y 0 (`top`) or 500: eight regions, a hidden highlight frame
    // and the cells.
    void Build(bool top);
    // selected_ becomes `item`'s place in the hero's +0x760
    // list, or -1.
    void ShowEntry(CItem* item);
    // The hero's +0x760 entry under (x, y) (regions 0-7, topic 1), or
    // null (topic 0 off the cells or while disabled).
    CItem* ItemAt(int x, int y, int* topic) override;
    // The highlight frame (scene object 0) on the
    // selected cell when it holds one of the hero's spell list (+0x760) entries, else hidden; each
    // entry's model in its cell; then the window's sprites and models. Returns 1.
    BOOL Draw(int pass) override;

    int y_;                          // +0x150 (mode_ 0, the cells' mode; owner_ the camp or the hero screen)
    int selected_;                   // +0x154 -1
};
static_assert(offsetof(CHeroQuickStrip, y_) == 0x150);
static_assert(sizeof(CHeroQuickStrip) == 0x158);

// The constructors' second pages: the spell maker (vtables, 0x16C bytes) and the
// item maker (0x158 bytes): the slots of what is being made beside a 2 x 4 grid.
class CConstructorPage : public CUiItemPanel {
public:
    // The item maker of `owner`.
    using CUiItemPanel::CUiItemPanel;
    BOOL Draw(int pass) override { return DrawItemMaker(pass); }                                      // 20
    CItem* ItemAt(int x, int y, int* topic) override { return ItemMakerItemAt(x, y, topic); }   // 25
    // The item maker: the blueprint and two part slots, the grid, the pictures
    // (constritem1..4) and a highlight frame.
    void BuildItemMaker(CParty* slots);
    // The window's sprites and models first; the
    // blueprint (250, 300) and the part (350, 250) drawn between depth clears of their slots, a
    // spell container at (350, 350), the materials down the grid; then, without a blueprint or a
    // container, the pulsing highlight on the blueprint and part slots (no part), the grid cells
    // the part has room for beyond the materials, or the container slot (none left). Returns 1.
    BOOL DrawItemMaker(int pass);
    // The slot item under
    // (x, y) and its help topic (0 off the regions or while disabled): region 0 the first spell
    // container (0x13), 1 the first spell (0x14), 2-9 modifier region - 2 (0x15), 10 / 11 the cost
    // lines (0x1A / 0x1B, no item); null when there is none.
    CItem* SpellMakerItemAt(int x, int y, int* topic);
    // The same: region 0 the first item that can be
    // taken apart (0xD), 1 the first part (0xE), 2 the first spell container (0x10), from 3 material
    // region - 3 (0xF).
    CItem* ItemMakerItemAt(int x, int y, int* topic);

    CParty* slots_;                  // +0x150 (mode_ 6 spell maker, 7 item maker; owner_ the camp) an item list (0x70 bytes)
    float pulse_;                    // +0x154 the highlight's time (0 when built)
};
static_assert(offsetof(CConstructorPage, slots_) == 0x150);
static_assert(offsetof(CConstructorPage, pulse_) == 0x154);
// The spell maker's cost texts ("infoitem_35", "infoitem_36"): a CStringArray at +0x158.
class CSpellMakerPage : public CConstructorPage {
public:
    // The spell maker of `owner` (inlined by the camp's Create): the cost texts' array.
    CSpellMakerPage(CInterface3D* ui, CItemScreen* owner) : CConstructorPage(ui, owner) { texts_.Construct(); }
    // The texts, then the window's.
    ~CSpellMakerPage() override;
    BOOL Draw(int pass) override { return DrawSpellMaker(pass); }                                      // 20
    CItem* ItemAt(int x, int y, int* topic) override { return SpellMakerItemAt(x, y, topic); }   // 25
    // The spell maker: the container and spell slots, the grid, the cost line's regions,
    // the four pictures (constrspell1..4), a highlight frame, the cost surface and its texts.
    void BuildSpellMaker(CParty* slots);
    // The slots' items (a spell
    // container at (250, 300), the spell at (350, 300), the modifiers down the 2 x 4 grid from
    // (450, 150)); the cost lines on the surface: "<text 0> <best skill> (<needed>)" and "<text 1>
    // <most mana> (<the larger of the summed and the last spell's mana>)" with a spell, else the
    // two texts; the window's sprites and models; then, without a container, the pulsing highlight
    // (scene object 4, its plain colour from cos(5 * pulse_)) on the empty grid cells (with a
    // spell) or on the container and spell slots. Returns 1.
    BOOL DrawSpellMaker(int pass);

    mfc::CStringArray texts_;        // +0x158
};
static_assert(offsetof(CSpellMakerPage, texts_) == 0x158);
static_assert(sizeof(CSpellMakerPage) == 0x16C);
