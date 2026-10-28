#pragma once

// The global map, the camp's pages and the hero panels (docs/notes/ui.md, "Camp, global map, hero screens").

#include "object_tables.h"
#include "options.h"
#include "ui.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/mfc_slots.h"
#include "lib/stlp.h"
#include "lib/vecmath.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class CConstructorPage;
class CGlobalMapClouds;
class CHeroEquipPanel;
class CHeroQuickStrip;
class CHeroStatsPanel;
class CRepairGrid;
class CSpellMakerPage;
class CTradeGrid;

// The hero panel's seven body slots' centres (800 x 600 layout).
inline constexpr int kBodySlotX[7] = /*00741EC4*/ {250, 250, 250, 550, 550, 550, 550};
inline constexpr int kBodySlotY[7] = /*00741EE0*/ {150, 300, 450, 150, 350, 450, 250};
inline constexpr float kGlobalMapFullTurn = /*007C8020*/ 6.2831855f;


// Every text section "pers <name>" gives its first line to the key of <name> (inserted, or replaced).
void LoadPersonTexts(PersonTextMap* map);

// The pixels the mouse must move with the button down on the view before the battle screen starts a
// selection box (0 or less: never).
inline int BoxSelectThreshold() { return options::TheOptions()->cameraFrameSelectionArea_; }

class ClassTemplate;

// The statistics' names, also their text keys.
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

// --- Global map ---
// The island map shown between zones (CreateScreen id 4). Regions: 0 / 1 turn the view, 2 the camp, 3.. the
// links between zones.

struct GlobalMapZone {
    int state;                       // the player's "z.<zone>" variable, truncated
    int link;                        // -1, or the link that leads to it (state 2)
    std::string name;
    std::string title;
};
struct GlobalMapZoneElements {
    static void Construct(GlobalMapZone* zones, int count);
    static void Destruct(GlobalMapZone* zones, int count);
    static void Relocate(GlobalMapZone* to, GlobalMapZone* from, int count) { mfc::MoveElements(to, from, count); }
};
struct GlobalMapZoneTable : mfc::CArray<GlobalMapZone, GlobalMapZoneElements> {
    void SetAtGrowZone(int index, const GlobalMapZone* zone);
};
using GlobalMapZoneArray = GlobalMapZoneTable;

// A quest place or a link (an "edge" zone) on the map.
struct GlobalMapPlace {
    GlobalMapPlace* Assign(const GlobalMapPlace& from);
    GlobalMapPlace* AssignCopy(const GlobalMapPlace& from);

    int zone;                        // index into the zones
    Vector3 position;                // on the map model
    std::string name;
    std::string title;
};
// The links' and the quests' elements: zero-filled, then both texts empty; destroyed title first.
struct GlobalMapPlaceElements {
    static void Construct(GlobalMapPlace* places, int count);
    static void Destruct(GlobalMapPlace* places, int count);
    static void Relocate(GlobalMapPlace* to, GlobalMapPlace* from, int count) { mfc::MoveElements(to, from, count); }
};
using GlobalMapPlaces = mfc::CArray<GlobalMapPlace, GlobalMapPlaceElements>;
// Their element destructors (title, then name).
void DestroyGlobalMapZone(GlobalMapZone* zone);
void DestroyGlobalMapPlace(GlobalMapPlace* place);

// The arrays' tables: the zones', the links' and the quests'.
using GlobalMapZoneRecords = mfc::impl::ArrayAbi<GlobalMapZone, GlobalMapZoneElements>;
using GlobalMapPlaceRecords = mfc::impl::ArrayAbi<GlobalMapPlace, GlobalMapPlaceElements>;
struct GlobalMapZoneArrayClass final
    : mfc::impl::CompiledCollection<GlobalMapZoneArrayClass, &GlobalMapZoneRecords::Destruct<GlobalMapZoneArrayClass>,
                                    &GlobalMapZoneRecords::Serialize> {};
struct GlobalMapPointArrayClass final
    : mfc::impl::CompiledCollection<GlobalMapPointArrayClass, &GlobalMapPlaceRecords::Destruct<GlobalMapPointArrayClass>,
                                    &GlobalMapPlaceRecords::Serialize> {};
struct GlobalMapPathArrayClass final
    : mfc::impl::CompiledCollection<GlobalMapPathArrayClass, &GlobalMapPlaceRecords::Destruct<GlobalMapPathArrayClass>,
                                    &GlobalMapPlaceRecords::Serialize> {};

// A zone on the way of the global map's way search and the exit taken.
struct GlobalMapStep {
    MapRecord* zone;
    int exit;
};

class CGlobalMap : public CUiContainer {
public:
    explicit CGlobalMap(CInterface3D* ui);
    ~CGlobalMap() override;
    BOOL Draw(CSceneRenderer* pass) override;   // the container's, blended
    void CloseScreen() override { Close(); }
    void MessageResult(int result) override { OnMessageResult(result); }
    void Deactivate() override { OnDeactivate(); }
    int LeftDown(int b, int x, int y) override { return OnLeftDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnLButtonUp(b, x, y); }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    int RightDown(int b, int x, int y) override { return OnRightDown(b, x, y), 0; }
    int RightUp(int b, int x, int y) override { return OnRightUp(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    // One zero dword.
    void SaveState() override;
    // The local player's allod: the zones, the quests the player holds ("q.<zone>.<quest>" = 1) and the
    // links to zones the player may enter ("z.<zone>" = 2).
    void CreateScreen() override;
    void Update() override;

    // Empty when there is no such text; `this` not read.
    std::string* AllodTitle(std::string* out, const char* allod);
    std::string* ZoneTitle(std::string* out, const char* zone);
    std::string* QuestTitle(std::string* out, const char* quest);
    // From map.txt (missing numbers 0). Not found leaves `out` as it is (the original writes its
    // uninitialised locals there). `this` not read.
    Vector3* ZonePosition(Vector3* out, const char* zone);
    Vector3* QuestPosition(Vector3* out, const char* quest);
    // `positionLine` looks for "#position" after the entry; false when map.txt has no such entry.
    static bool ReadMapPosition(Vector3* out, const char* format, const char* name, bool positionLine);
    // On gipat a zone and its link are selected together. -1 selects nothing.
    void SelectZone(int index, bool isLink);
    // Applies weatherTable_: the display's colour, the three layers' colours (ARGB from four ints, alpha
    // last) and the weather object's ramp.
    void SetWeather();
    // Renders the pick picture (the zone markers alone in plain colours 0xFF181818 + 0x101010 * i) into the
    // backdrop; a click reads the zone back from its pixel. Nothing without a backdrop.
    void RefreshSelection();
    // The way search, recursive: arrivals_ keeps the shortest time per exit into target_; the way goes on
    // through a zone the player may pass ("z.<zone>" 2 for a game zone, not 1 for an edge, not 0 for a brief
    // zone), pathTime_ growing by the exit's "#passtime".
    void FindPaths(MapRecord* from);
    // The right button drags the map round.
    void OnRightDown(int buttons, int x, int y);
    void OnRightUp(int buttons, int x, int y);
    void OnDeactivate();
    // Rebirth-patched (WideScreen behind its flag: the drag and the pick picture's x without the margin).
    // Dragging turns the map by dx / 300 layout units; otherwise the zone or link under the mouse is
    // selected. Details: docs/notes/ui.md, "Behaviour moved out of header comments".
    void OnMouseMove(int buttons, int x, int y);
    // Rebirth's WideScreen patch reproduced (the pick picture's x without the margin). Nothing while
    // dragging; a zone also opens the objectives window. Details: docs/notes/ui.md, "Behaviour moved out of
    // header comments".
    void OnLeftDown(int buttons, int x, int y);
    // Esc opens the escape menu; an unused key whose action is '5' quick-saves ("saveq"), '6' quick-loads.
    void OnKeyDown(int key, int repeat, int flags);
    // A window above closed.
    void OnMessageResult(int result);
    const char* HelpTopic() override;
    // Ends a turn by a turn button.
    int OnLButtonUp(int buttons, int x, int y);
    // Always true; neither the map nor the argument is read.
    bool MayTravel(int unused);
    void Close();
    // Zones, then quests, then links, then the three layers.
    CUiMesh* Marker(int index) const { return SceneMesh(3 + index); }
    // Scene objects 0..2 and those after the zone markers.
    void ShowAllButZones(int visible);

    int zoneCount_;
    int questCount_;
    int linkCount_;
    int markerCount_;                // zones + quests + links
    int turning_;                    // -1 / +1 while a turn button is held
    float bobTime_;                  // seconds, for the arrow's bobbing and the layers' frames
    std::string allodTitle_;
    Vector3 position_;               // the current zone on the map model
    GlobalMapZoneArray zones_;
    GlobalMapPlaces links_;
    GlobalMapPlaces quests_;
    // The way search from the current zone to target_: the zones on the way so far and their summed
    // "#passtime".
    std::vector<GlobalMapStep> path_;
    float pathTime_;
    std::string target_;
    bool found_;                     // a way reached the target
    bool beyondTarget_;              // the search goes on through the target zone itself
    std::uint8_t unused_1DE[2];
    stlp::map<int, float> arrivals_; // target exit -> shortest time (-1: next to the current zone)
    Quaternion turn_;                // the model's orientation
    float angle_;                    // the model's turn about z (mod kGlobalMapFullTurn)
    float depth_;                    // 10
    float centreX_;                  // 400 (layout)
    float centreY_;                  // 280
    int selectPending_;              // 1: the zone selection is refreshed on the next update
    CUiSurface* backdrop_;           // a full-screen surface (not in the surface list)
    CGlobalMapClouds* weather_;
    int weatherTable_[18];
    int weatherIndex_;
    bool dragging_;                  // the map is turned with the mouse
    std::uint8_t unused_265[3];
    POINT dragStart_;                // where the drag began (layout)
    float dragAngle_;                // angle_ when the drag began
    std::string shownName_;         // the zone or link whose tip is shown
    bool gipat_;                     // the allod is gipat
    std::uint8_t unused_279[3];
    int noCamp_;                     // the player's "i.nocamp" (1: no camp button)
    float fog_[3];                   // by allod
    bool autosave_;                  // save "saveb" once built (arrived from a zone)
    std::uint8_t unused_28D[3];
    int windSound_;
};
static_assert(offsetof(CGlobalMap, zoneCount_) == 0x164);
static_assert(offsetof(CGlobalMap, allodTitle_) == 0x17C);

// --- Camp trade panel (the level-up page) ---
// Sells skills for experience: the hero's six magic schools and science, the perks the hero holds and the
// perks the hero may learn, each with its price.

struct TradeRow {
    std::string name;
    std::string key;                // the perk's record name (perk rows)
    std::string nextKey;            // the next level's record name (held perks below 3)
    int price;                       // experience
    int level;
    bool learnable;                  // set on the learnable list (not written in the others)
    std::uint8_t unused_15[3];
};

using TradeRows = std::vector<TradeRow>;

// A party member's statistics as the trade panel keeps them (filled from the local player's unit record
// `index`).
struct TradeRecord {
    std::uint8_t science;            // stats +0x6C8
    std::uint8_t schools[6];         // stats +0x6CB..
    std::uint8_t perks[36];          // stats +0x6D3..
    std::uint8_t padAfterPerks;      // not written
    float experience;                // stats +0x04
    float spentExperience;           // stats +0x08
    float pendingExperience;         // stats +0x700
    std::uint8_t bought;             // 0; 1 once something was bought on the panel
    std::uint8_t padAfterBought[3];  // not written
};
static_assert(sizeof(TradeRecord) == 0x3C);

class CTradePanel : public CUiItemPanel {
public:
    CTradePanel(CInterface3D* ui, CItemScreen* owner) : CUiItemPanel(ui, owner) {}
    ~CTradePanel() override;
    void Update() override { UpdateHeld(); }
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnLButtonUp(b, x, y), 0; }
    // One statistics record per party member; the selected record -1.
    void Build() override;
    // The schools, the held perks (a level byte per group of three skill records) and every first-level perk
    // the hero may learn, for the owner's hero.
    void BuildLists();
    // Prices over the experience in grey.
    void DrawLists();
    // "%d" below 1000, "%.1fk" below 10000, "%dk" below a million, "%.1fm" below ten million, "%dm" below
    // 1e9, else "%.1fg"; a negative price shows as 0x7FFFFFFF.
    void FormatPrice(int price, std::string* out);
    // Regions 4-9 the schools (kind 1), 10 science (kind 2), 11 up the perks (kind 3: the perk's record name
    // without its level digits, plus "0"; held ones with their level).
    void HoverInfo(int x, int y, ItemHoverInfo* info);
    // One record per party member (recordIndex_ is left at the last).
    void ResetRecords();
    // Record recordIndex_ (when >= 0) from the party member's statistics; bought cleared.
    void SnapshotRecord();
    // The reverse; when the hit point maximum was and is above 1, the slots are healed by what keeps the hit
    // points in proportion.
    void RestoreRecord();
    // With `all`, every bought record is kept (with `keep`) or undone; otherwise only the current record when
    // bought.
    void Settle(int keep, int all);
    // Scroll positions 0.
    void SelectHero(int index);
    // Pending plus earned minus spent, truncated.
    int Experience() const;
    // The cost of the perks bought on the panel since the record was made, truncated.
    int PurchaseCost();
    // The scroll arrows show only where the list can scroll.
    void ClampScroll();
    // Regions 0-3 scroll; from 4 a purchase the experience covers (a school or science row is then held),
    // the record marked bought.
    void OnLButtonDown(int buttons, int x, int y);
    // Releases the mouse unconditionally and ends a hold.
    void OnLButtonUp(int buttons, int x, int y);
    // From 0.25 s of a hold on, every frame buys the row's next level again (the record is not marked
    // bought) until it cannot. The hold time is not reset after a purchase.
    void UpdateHeld();
    std::uint8_t Bought() const;
    const TradeRecord& Record() const { return records_[recordIndex_]; }

    int recordIndex_;
    int learnableTop_;               // first learnable row shown
    int perkTop_;                    // first held perk shown
    TradeRows learnable_;
    TradeRows perks_;
    TradeRows schools_;              // six
    TradeRows science_;              // one
    int experience_;                 // experience the hero can spend
    std::vector<TradeRecord> records_;            // one per party member
    int heldRegion_;                 // the school / science row held after a purchase
    float holdTime_;                 // from -0.5 s
    std::vector<std::string> texts_; // the titles
};
static_assert(offsetof(CTradePanel, owner_) == 0x14C);
static_assert(offsetof(CTradePanel, learnable_) == 0x15C);
static_assert(offsetof(CTradePanel, science_) == 0x180);
static_assert(offsetof(CTradePanel, experience_) == 0x18C);
static_assert(offsetof(CTradePanel, texts_) == 0x1A4);
static_assert(offsetof(CTradePanel, records_) == 0x190);
static_assert(offsetof(CTradePanel, holdTime_) == 0x1A0);

// --- Camp pages ---
// The camp: the party's equipment, trade, repair and the item and spell constructors, with three item
// strips; also opened by the multiplayer hero screen for a hero's inventory. Pages: 0 equipment, 1 trade, 2
// spell constructor, 3 its second page, 4 item constructor, 5 and 6 its other pages, 7 leave the camp.
class CCampPages : public CItemScreen {
public:
    CCampPages(CInterface3D* ui, bool fromHeroScreen);
    ~CCampPages() override;
    void CloseScreen() override { Close(); }
    void MessageResult(int result) override { OnMessageResult(result); }
    void TakeItem(CItem* item, int mode) override { MoveItem(item, mode); }
    void TopicClicked(int page, int unused) override { ShowPageTabs(page, unused); }
    CUnit* HeroUnit(int index) override { return HeroAt(index); }
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnLButtonUp(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    int KeyUp(int k, int r, int f) override { return OnKeyUp(k, r, f), 0; }
    // Over the party's heroes, the best skill in spell school `school` (rounded to nearest) and the most
    // mana units (maximum mana / period, truncated).
    void BestPartySkill(int school, int* skill, int* mana) override;
    // Camp page `page` (1-21).
    void ShowPageTabs(int page, int unused);
    // Rebirth-patched: BuildPages, then with WideScreen the two curtains over the screen's margins.
    void CreateScreen() override;
    // Create's body. Without the player's "constr.current" class template: back to the world, nothing built.
    // Details: docs/notes/ui.md, "Behaviour moved out of header comments".
    void BuildPages();
    // A constructor page the camp does not offer falls back to the current page. Page 7 gives the
    // constructors' unused items back and leaves the camp.
    void ShowPage(int page);
    // Not checked.
    PartyUnitRecord* HeroRecord() override;
    // Whether `item` may go where the strip with action `mode` puts it on the current page (for the
    // constructors: the best hero's skill and mana, at most eight modifiers, matching blueprints).
    bool CanEquip(CItem* item, int mode) override;
    // The page's action and its cost against the money (experience on the trade page).
    void UpdateAction();
    // Keeps what the constructor makes in spellMade_ (itemMade_, with a reference); true when it can be made.
    bool CanMakeSpell();
    bool CanMakeItem();
    // The item's base price times the camp's multiplier for deal `kind`; weapons and armour also lose the
    // repair cost of their wear. A repair (kind 8) costs at least 1, or 0 for an unworn item. Rebirth's
    // FixRepairPrice clamps the wear to [0, 1] and adds the repair cost instead of subtracting it when an
    // item is taken apart (kind 7).
    int Price(CItem* item, int kind) override;
    // Also finds what is under the mouse for the description panel.
    BOOL Draw(CSceneRenderer* pass) override;
    // Hands the local player's unit records back (their ids were raised by 100000000 for the call) and
    // deletes the constructors' part lists except the first of each.
    void Close();
    // A weapon 0x3004 or armour 0x3005, 0x3006 or 0x3008 off the current hero into the player's items
    // (whose result it returns; 0 for other kinds).
    CItem* TakeOff(CItem* item);
    // A weapon (at most 4), armour (the same body part's taken off first), a 0x3006 item (at most 4) or a
    // 0x3008 item (at most 8). Other kinds and full slots do nothing.
    void PutOn(CItem* item);
    // Pages 0 / 1 put the item on the hero; the others move it into the page's slots (with Shift and a
    // kind-one item the whole stack).
    void PutOnFromStrip(CItem* item);
    // What an item strip or a panel calls with its action mode: 0 off the hero, 1 PutOnFromStrip, 2 / 3 into
    // a constructor's slots, 4 / 5 out of a constructor's trade lists, 6 / 7 out of the makers' slots, 8 out
    // of itemExtra_. Refused by CanEquip: the cancel sound.
    void MoveItem(CItem* item, int mode);
    void RefreshWeapons() override;
    void RefreshStats() override;
    // The hero preview turns while a turn button is held.
    void Update() override;
    // Wrapping; fatal after a full round without a hero (prototype class 0x32).
    void NextHero() override;
    void PreviousHero() override;
    // The unit's own name, else the text of "pers <record name>".
    void ShowHero();
    // Regions 0 / 1 the deals, 2 / 3 turn the hero on the equipment page, 4-11 show page region - 4.
    void OnLButtonDown(int buttons, int x, int y);
    void OnLButtonUp(int buttons, int x, int y);
    // Esc shows page 7 (leaves); on a network client Backspace and Enter go to the chat line.
    void OnKeyDown(int key, int repeat, int flags);
    void OnKeyUp(int key, int repeat, int flags);
    // Not checked.
    CUnit* HeroAt(int index);
    // 0x10001 takes the shown hero's first weapon back into the party's items.
    void OnMessageResult(int result);
    const char* HelpTopic() override;
    // By page: 0 repairs the hero's weapons and armour, 1 settles the trade (the money is left alone), 2 / 4
    // a constructor buys its third list, 3 / 5 the made spell / item (or the parts while deconstructing_),
    // 6 the repairs. Then the money goes down by cost_.
    void DoAction();
    // By page: 1 the trade settled with (0, 0); 2-6 the lists given back.
    void UndoAction();
    // A "Prototype" item for the container's spell and a "Rune" item per modifier.
    bool DeconstructSpell();
    // An "Instruction Weapon / Armor / Quick Item", a "material.<key>" per material and for an item with a
    // spell a "spell container" holding a clone of it.
    bool DeconstructItem();

    // The console's "give <player> assortment": every instruction, material and rune into the constructors'
    // first lists.
    void DebugFill() override;


    int page_;
    bool fromHeroScreen_;            // opened from the multiplayer hero screen (closes with a pending value)
    bool spellConstructor_;          // the spell constructor's pages are offered
    bool itemConstructor_;           // the item constructor's pages are offered
    std::uint8_t unused_197;
    ClassTemplate* constructor_;     // the constructor's class template ("constr.current"), or null
    // A unit per party record, null for other classes.
    mfc::CArray<CUnit*> heroes_;
    int hero_;                       // the hero shown
    // The constructors' parts and what they made.
    CParty* itemParts_[3];
    CParty* itemResults_;
    CParty* itemExtra_;
    CParty* spellParts_[3];
    CParty* spellResults_;
    CHeroStatsPanel* statsPanel_;
    CItemInfoPanel* helpPanel_;
    CUiItemStrip* itemStrip_;
    CHeroEquipPanel* equipment_;
    CHeroQuickStrip* equipment2_;
    CUiItemStrip* spellStrip_;
    CUiItemStrip* partStrip_;
    CTradePanel* trade_;
    CTradeGrid* spellConstructor1_;
    CSpellMakerPage* spellConstructor2_;
    CTradeGrid* itemConstructor1_;
    CConstructorPage* itemConstructor2_;
    CRepairGrid* itemConstructor3_;
    int cost_;                       // what the action costs (negative: earns)
    bool actionEnabled_;             // the action button can be pressed
    bool actionShown_;
    bool deconstructing_;            // the constructor takes an item apart
    std::uint8_t unused_213;
    CItem* itemMade_;                // the item the item constructor would make, or null
    CItem* spellMade_;               // the spell the spell constructor would make, or null
    float priceWeights_[9];          // ClassTemplate::weights
    std::vector<std::string> pageTitles_;   // the seven pages' titles and "quit"
    bool noConstructor_;             // the player's "i.noconstr" = 1: second pages hidden
    std::uint32_t heroTurn_;         // passed to the hero preview
    int turning_;                    // 1 / 2 while a turn button is held (Update turns the hero)
};
static_assert(offsetof(CCampPages, page_) == 0x190);
static_assert(offsetof(CCampPages, hero_) == 0x1B0);
static_assert(offsetof(CCampPages, spellResults_) == 0x1D4);
static_assert(offsetof(CCampPages, statsPanel_) == 0x1D8);
static_assert(offsetof(CCampPages, trade_) == 0x1F4);
static_assert(offsetof(CCampPages, itemConstructor3_) == 0x208);
static_assert(offsetof(CCampPages, pageTitles_) == 0x240);
static_assert(offsetof(CCampPages, heroes_) == 0x19C);
static_assert(offsetof(CCampPages, priceWeights_) == 0x21C);

// --- Hero statistics panel ---
// The statistics column of the camp and the multiplayer hero screen.
class CHeroStatsPanel : public CUiItemPanel {
public:
    CHeroStatsPanel(CInterface3D* ui, CItemScreen* owner) : CUiItemPanel(ui, owner) {}
    ~CHeroStatsPanel() override;
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnLButtonUp(b, x, y); }
    // Page 0 the body parts and the weapon, 10 the new hero's points with -/+ buttons, others the plain
    // values.
    void ShowStats(int page);
    void DrawStats();
    void Build() override;
    // Page 0: rows 2-5 the body parts' armour, 6 / 7 the shown weapon's attack / defence, 8 the damage, 9
    // encumbrance, 10 the magic skills; other pages the attributes (page 10: row 14 the points left, row 5
    // the height).
    void HoverInfo(int x, int y, ItemHoverInfo* info);
    // `title` may be null; the hero arrows show only for a party of more than one; points_ = the hero's
    // height * 100 (truncated).
    void SetHero(int heroIndex, const char* title);
    // On page 10 a -/+ button (regions 6-13) steps a point and is held; elsewhere region 0 / 1 show the
    // owner's previous / next hero.
    void OnLButtonDown(int buttons, int x, int y);
    // A held -/+ button repeats every frame after a quarter of a second until it can step no more; the owner
    // is refreshed on every attempt.
    void Update() override;
    // Lets the mouse go unconditionally and ends the hold.
    int OnLButtonUp(int buttons, int x, int y);
    // Regions 6-8 take a point from strength, dexterity, intelligence (above 15), 9 the height down
    // (points_ >= 1), 10-12 give one (below 35, while the three add up to under 75), 13 the height up
    // (points_ <= 99; height = points_ * 0.01). True when it changed something.
    bool StepPoint(int region);
    // The text of "perk <label>" (else `label` itself), `value` as "%d", or "%d/%d" with a maximum >= 0.
    void DrawRow(RECT* rect, const char* label, int value, int maximum);
    void DrawPart(RECT* rect, const char* name, int part);
    // Per damage type the three layers' absorption summed (stored as float); the largest of the first six.
    float PartArmour(int part);

    int heroIndex_;
    std::string title_;
    int page_;
    std::vector<std::string> texts_;
    int points_;                     // skill points left (page 10); SetHero puts the height * 100 here
    int heldRegion_;                 // the -/+ button held on page 10 (-1 none)
    float holdTime_;                 // seconds it has been held (from -0.5)
};
static_assert(offsetof(CHeroStatsPanel, owner_) == 0x14C);
static_assert(offsetof(CHeroStatsPanel, title_) == 0x154);

// --- Hero equipment panel ---
// The eight quick slots along the top, and in the camp the body with its seven equipment slots.
class CHeroEquipPanel : public CUiItemPanel {
public:
    using CUiItemPanel::CUiItemPanel;
    BOOL Draw(CSceneRenderer* pass) override;
    int RightDown(int b, int x, int y) override { return OnRightDown(b, x, y), 0; }
    // With `body` (the camp) the panel is 500 high with the seven body slots.
    void Build(bool body);
    // The place in the hero's weapon list, or -1.
    void ShowWeapon(CItem* weapon);
    // The place in the hero's armour list, or -1.
    void ShowArmour(CItem* item);
    // On the camp's body: a right click on a filled weapon slot (of the first four) shows that weapon. The
    // event is not marked used.
    void OnRightDown(int buttons, int x, int y);
    // Regions 0-3 the weapons (topic 2), 4-7 the armour list from its end (3), body regions 8-14 the armour
    // worn at body part region - 8 (topic region - 4).
    CItem* ItemAt(int x, int y, int* topic) override;

    bool body_;
    std::uint8_t unused_151[3];
    int selected_;                   // -1
};
static_assert(offsetof(CHeroEquipPanel, body_) == 0x150);
static_assert(sizeof(CHeroEquipPanel) == 0x158);

// A party's or trader's items.
using UiItemList = lib::AlignedArray<CItem*>;
// The 4 x 4 grid of item cells on the constructors' first pages.
class CTradeGrid : public CUiItemPanel {
public:
    using CUiItemPanel::CUiItemPanel;
    BOOL Draw(CSceneRenderer* pass) override { return DrawGrid(pass); }
    int DrawGrid(CSceneRenderer* pass);
    // `spells` chooses the price kinds (0/1 for the spell constructor, 4/5 for the item one).
    void Build(UiItemList* bought, UiItemList* sold, char spells, char flag);
    // Regions 0-7 the bought list (topic 0x1C with flag_, else 0x0B for the item constructor, 0x11 for the
    // spell one), 8-15 the sold list (0x1D / 0x0C / 0x12).
    CItem* ItemAt(int x, int y, int* topic) override;

    UiItemList* bought_;
    UiItemList* sold_;
    int buyKind_;
    int sellKind_;
    char flag_;
    std::uint8_t unused_161[3];
};
static_assert(offsetof(CTradeGrid, bought_) == 0x150);
static_assert(sizeof(CTradeGrid) == 0x164);

// The repair page's grid: the same cells as the trade grid with textures repair1..4.
class CRepairGrid : public CUiItemPanel {
public:
    using CUiItemPanel::CUiItemPanel;
    void Build(UiItemList* items);
    // Each item's model in its cell (depth 2^(3 - row) * 6), its count and its repair price.
    BOOL Draw(CSceneRenderer* pass) override;
    // Topic 0x19 on the 16 cells.
    CItem* ItemAt(int x, int y, int* topic) override;

    UiItemList* items_;
};
static_assert(offsetof(CRepairGrid, items_) == 0x150);
static_assert(sizeof(CRepairGrid) == 0x154);

// The quick item strip of the hero panels: eight cells along the top (camp) or the bottom of the screen.
class CHeroQuickStrip : public CUiItemPanel {
public:
    using CUiItemPanel::CUiItemPanel;
    // At y 0 (`top`) or 500.
    void Build(bool top);
    // The place in the hero's spell list, or -1.
    void ShowEntry(CItem* item);
    // The hero's spell list entry (topic 1).
    CItem* ItemAt(int x, int y, int* topic) override;
    BOOL Draw(CSceneRenderer* pass) override;

    int y_;
    int selected_;                   // -1
};
static_assert(offsetof(CHeroQuickStrip, y_) == 0x150);
static_assert(sizeof(CHeroQuickStrip) == 0x158);

// The constructors' second pages: the spell maker and the item maker: the slots of what is being made beside
// a 2 x 4 grid.
class CConstructorPage : public CUiItemPanel {
public:
    using CUiItemPanel::CUiItemPanel;
    BOOL Draw(CSceneRenderer* pass) override { return DrawItemMaker(pass); }
    CItem* ItemAt(int x, int y, int* topic) override { return ItemMakerItemAt(x, y, topic); }
    void BuildItemMaker(CParty* slots);
    // Without a blueprint or a container, a pulsing highlight shows where the next part goes.
    BOOL DrawItemMaker(CSceneRenderer* pass);
    // Region 0 the first spell container (topic 0x13), 1 the first spell (0x14), 2-9 modifier region - 2
    // (0x15), 10 / 11 the cost lines (0x1A / 0x1B, no item).
    CItem* SpellMakerItemAt(int x, int y, int* topic);
    // Region 0 the first item that can be taken apart (0xD), 1 the first part (0xE), 2 the first spell
    // container (0x10), from 3 material region - 3 (0xF).
    CItem* ItemMakerItemAt(int x, int y, int* topic);

    CParty* slots_;
    float pulse_;                    // the highlight's time
};
static_assert(offsetof(CConstructorPage, slots_) == 0x150);
static_assert(offsetof(CConstructorPage, pulse_) == 0x154);
class CSpellMakerPage : public CConstructorPage {
public:
    CSpellMakerPage(CInterface3D* ui, CItemScreen* owner) : CConstructorPage(ui, owner) {}
    ~CSpellMakerPage() override;
    BOOL Draw(CSceneRenderer* pass) override { return DrawSpellMaker(pass); }
    CItem* ItemAt(int x, int y, int* topic) override { return SpellMakerItemAt(x, y, topic); }
    void BuildSpellMaker(CParty* slots);
    // The cost lines: "<text 0> <best skill> (<needed>)" and "<text 1> <most mana> (<the larger of the summed
    // and the last spell's mana>)" with a spell. Without a container, a pulsing highlight shows where the
    // next part goes.
    BOOL DrawSpellMaker(CSceneRenderer* pass);

    std::vector<std::string> texts_; // the cost texts ("infoitem_35", "infoitem_36")
};
static_assert(offsetof(CSpellMakerPage, texts_) == 0x158);
