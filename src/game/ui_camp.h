#pragma once

// The global map, the camp's pages and the hero panels (docs/notes/ui.md, "Camp, global map, hero screens").

#include "object_tables.h"
#include "options.h"
#include "ui.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include "lib/crt.h"
#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

class ConstructorPage;
class GlobalMapClouds;
class HeroEquipPanel;
class HeroQuickStrip;
class HeroStatsPanel;
class RepairGrid;
class SpellMakerPage;
class TradeGrid;

// The hero panel's seven body slots' centres (800 x 600 layout).
inline constexpr int kBodySlotX[7] = /*00741EC4*/ {250, 250, 250, 550, 550, 550, 550};
inline constexpr int kBodySlotY[7] = /*00741EE0*/ {150, 300, 450, 150, 350, 450, 250};
inline constexpr float kGlobalMapFullTurn = /*007C8020*/ 6.2831855f;

// Every text section "pers <name>" gives its first line to the key of <name> (inserted, or replaced).
void LoadPersonTexts(std::map<int, std::string>* map);

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
// A quest place or a link (an "edge" zone) on the map.
struct GlobalMapPlace {
    int zone;                        // index into the zones
    Vector3 position;                // on the map model
    std::string name;
    std::string title;
};
// A zone on the way of the global map's way search and the exit taken.
struct GlobalMapStep {
    MapRecord* zone;
    int exit;
};

class GlobalMap : public UiContainer {
public:
    using UiContainer::UiContainer;
    void Draw(SceneRenderer* pass) override;   // the container's, blended
    // The clouds and the backdrop freed, the wind stopped.
    void CloseScreen() override;
    // One zero dword.
    void SaveState() override;
    // The local player's allod: the zones, the quests the player holds ("q.<zone>.<quest>" = 1) and the
    // links to zones the player may enter ("z.<zone>" = 2).
    void CreateScreen() override;
    void Update() override;

    // The first line of the text `prefix` + `name` ("allod ", "zone ", "quest "); empty when there is none.
    static std::string PlaceTitle(std::string_view prefix, std::string_view name);
    // From map.txt: the numbers on the line after the entry `prefix` + `name` (with `positionLine`, after the
    // entry's "#position" line; missing numbers 0); none when map.txt has no such entry (the original left
    // its uninitialised locals).
    static std::optional<Vector3> MapPosition(std::string_view prefix, std::string_view name, bool positionLine);
    // On gipat a zone and its link are selected together. -1 selects nothing.
    void SelectZone(int index, bool isLink);
    // Renders the pick picture (the zone markers alone in plain colours 0xFF181818 + 0x101010 * i) into the
    // backdrop; a click reads the zone back from its pixel. Nothing without a backdrop.
    void RefreshSelection();
    // The way search, recursive: arrivals keeps the shortest time per exit into target; the way goes on
    // through a zone the player may pass ("z.<zone>" 2 for a game zone, not 1 for an edge, not 0 for a brief
    // zone), pathTime growing by the exit's "#passtime".
    void FindPaths(MapRecord* from);
    // The ways from the local player's zone to `target`, the earlier results dropped.
    void SearchWays(const std::string& target);
    // The right button drags the map round; letting it go ends the drag as a deactivation does.
    void RightDown(int buttons, int x, int y) override;
    void RightUp(int, int, int) override { Deactivate(); }
    // The drag ends, the cursor goes back to where it began; the selection is refreshed.
    void Deactivate() override;
    // Rebirth-patched (WideScreen behind its flag: the drag and the pick picture's x without the margin).
    // Dragging turns the map by dx / 300 layout units; otherwise the zone or link under the mouse is
    // selected. Details: docs/notes/ui.md, "Behaviour moved out of header comments".
    void MouseMove(int buttons, int x, int y) override;
    // Rebirth's WideScreen patch reproduced (the pick picture's x without the margin). Nothing while
    // dragging; a zone also opens the objectives window. Details: docs/notes/ui.md, "Behaviour moved out of
    // header comments".
    void LeftDown(int buttons, int x, int y) override;
    // Esc opens the escape menu; an unused key whose action is '5' quick-saves ("saveq"), '6' quick-loads.
    void KeyDown(int key, int repeat, int flags) override;
    // A window above closed.
    void MessageResult(int result) override;
    const char* HelpTopic() override { return noCamp ? "global_map_nocamp" : "global_map"; }
    // Ends a turn by a turn button.
    void LeftUp(int buttons, int x, int y) override;
    // Zones, then quests, then links, then the three layers.
    UiMesh* Marker(int index) const { return SceneMesh(3 + index); }
    // Scene objects 0..2 and those after the zone markers.
    void ShowAllButZones(bool visible);
    // Zones + quests + links: the layers and the rest follow them.
    int MarkerCount() const { return static_cast<int>(zones.size() + quests.size() + links.size()); }

    int turning = 0;                 // -1 / +1 while a turn button is held
    float bobTime = 0.0f;            // seconds, for the arrow's bobbing and the layers' frames
    std::string allodTitle;
    Vector3 position{};              // the current zone on the map model
    std::vector<GlobalMapZone> zones;
    std::vector<GlobalMapPlace> links;
    std::vector<GlobalMapPlace> quests;
    // The way search from the current zone to target: the zones on the way so far and their summed
    // "#passtime".
    std::vector<GlobalMapStep> path;
    float pathTime = 0.0f;
    std::string target;
    bool found = false;              // a way reached the target
    bool beyondTarget = false;       // the search goes on through the target zone itself
    std::map<int, float> arrivals; // target exit -> shortest time (-1: next to the current zone)
    Quaternion turn{1.0f, 0.0f, 0.0f, 0.0f};    // the model's orientation (the identity until built)
    float angle = 0.0f;              // the model's turn about z (mod kGlobalMapFullTurn)
    float depth = 10.0f;
    float centreX = 400.0f;          // layout
    float centreY = 280.0f;
    bool selectPending = false;      // the zone selection is refreshed on the next update
    UiSurface* backdrop = nullptr; // a full-screen surface (not in the surface list)
    GlobalMapClouds* weather = nullptr;
    bool dragging = false;           // the map is turned with the mouse
    POINT dragStart{};               // where the drag began (layout)
    float dragAngle = 0.0f;          // angle when the drag began
    std::string shownName;          // the zone or link whose tip is shown
    bool gipat = false;              // the allod is gipat
    bool noCamp = false;             // the player's "i.nocamp" is 1: no camp button
    float fog[3] = {0.0f, 1.0f, 0.0f};    // the selected zone's tint, by allod
    int windSound = 0;
};

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
};

// A party member's statistics as the trade panel keeps them (filled from the local player's unit record
// `index`), to undo what was bought on the panel.
struct TradeStatsRecord {
    std::uint8_t science = 0;
    std::uint8_t schools[6] = {};
    std::uint8_t perks[36] = {};
    float experience = 0.0f;
    float spentExperience = 0.0f;
    float pendingExperience = 0.0f;
    bool bought = false;             // something was bought on the panel since the snapshot
};

class TradePanel : public UiItemPanel {
public:
    TradePanel(Interface3D* ui, ItemScreen* owner) : UiItemPanel(ui, owner) {}
    // One statistics record per party member; the selected record -1.
    void Build() override;
    // The schools, the held perks (a level byte per group of three skill records) and every first-level perk
    // the hero may learn, for the owner's hero.
    void BuildLists();
    // Prices over the experience in grey.
    void DrawLists();
    // "%d" below 1000, "%.1fk" below 10000, "%dk" below a million, "%.1fm" below ten million, "%dm" below
    // 1e9, else "%.1fg"; a negative price shows as 0x7FFFFFFF.
    static std::string FormatPrice(int price);
    // Regions 4-9 the schools (Skill), 10 science (Science), 11 up the perks (Perk: the perk's record name
    // without its level digits, plus "0"; held ones with their level).
    ItemHoverInfo HoverInfo(int x, int y);
    // One record per party member (recordIndex is left at the last).
    void ResetRecords();
    // Record recordIndex (when >= 0) from the party member's statistics; bought cleared.
    void SnapshotRecord();
    // The reverse; when the hit point maximum was and is above 1, the slots are healed by what keeps the hit
    // points in proportion.
    void RestoreRecord();
    // With `all`, every bought record is kept (with `keep`) or undone; otherwise only the current record when
    // bought.
    void Settle(bool keep, bool all);
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
    void LeftDown(int buttons, int x, int y) override;
    // Releases the mouse unconditionally and ends a hold.
    void LeftUp(int buttons, int x, int y) override;
    // From 0.25 s of a hold on, every frame buys the row's next level again (the record is not marked
    // bought) until it cannot. The hold time is not reset after a purchase.
    void Update() override;
    bool Bought() const { return Record().bought; }
    const TradeStatsRecord& Record() const { return records[recordIndex]; }

    int recordIndex = 0;
    int learnableTop = 0;            // first learnable row shown
    int perkTop = 0;                 // first held perk shown
    std::vector<TradeRow> learnable;
    std::vector<TradeRow> perks;
    std::vector<TradeRow> schools;               // six
    std::vector<TradeRow> science;               // one
    int experience = 0;              // experience the hero can spend
    std::vector<TradeStatsRecord> records;    // one per party member
    int heldRegion = -1;             // the school / science row held after a purchase
    float holdTime = 0.0f;           // from -0.5 s
    std::vector<std::string> texts; // the titles
};

// --- Camp pages ---
// The camp: the party's equipment, trade, repair and the item and spell constructors, with three item
// strips; also opened by the multiplayer hero screen for a hero's inventory. Pages: 0 equipment, 1 trade, 2
// spell constructor, 3 its second page, 4 item constructor, 5 and 6 its other pages, 7 leave the camp.
class CampPages : public ItemScreen {
public:
    CampPages(Interface3D* ui, bool newFromHeroScreen) : ItemScreen(ui), fromHeroScreen(newFromHeroScreen) {}
    // Hands the local player's unit records back (their ids were raised by 100000000 for the call) and
    // deletes the constructors' part lists except the first of each.
    void CloseScreen() override;
    // 0x10001 takes the shown hero's first weapon back into the party's items.
    void MessageResult(int result) override;
    // What an item strip or a panel calls with its action mode: 0 off the hero, 1 PutOnFromStrip, 2 / 3 into
    // a constructor's slots, 4 / 5 out of a constructor's trade lists, 6 / 7 out of the makers' slots, 8 out
    // of itemExtra. Refused by CanEquip: the cancel sound.
    void TakeItem(Item* item, int mode) override;
    // Camp page `page` (1-21).
    void TopicClicked(int page) override;
    // Not checked.
    CUnit* HeroUnit(int index) override { return heroes[index]; }
    // Over the party's heroes, the best skill in spell school `school` (rounded to nearest) and the most
    // mana units (maximum mana / period, truncated).
    PartySkill BestPartySkill(int school) override;
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
    bool CanEquip(Item* item, int mode) override;
    // The page's action and its cost against the money (experience on the trade page).
    void UpdateAction();
    // Keeps what the constructor makes in spellMade (itemMade); true when it can be made.
    bool CanMakeSpell();
    bool CanMakeItem();
    // The item's base price times the camp's multiplier for deal `kind`; weapons and armour also lose the
    // repair cost of their wear. A repair (kind 8) costs at least 1, or 0 for an unworn item. Rebirth's
    // FixRepairPrice clamps the wear to [0, 1] and adds the repair cost instead of subtracting it when an
    // item is taken apart (kind 7).
    int Price(Item* item, int kind) override;
    // Also finds what is under the mouse for the description panel.
    void Draw(SceneRenderer* pass) override;
    // A weapon, armour, quick item or spell container off the current hero into the player's items (whose
    // result it returns; null for other kinds).
    Item* TakeOff(Item* item);
    // A weapon (at most 4), armour (the same body part's taken off first), a quick item (at most 4) or a
    // spell container (at most 8). Other kinds and full slots do nothing.
    void PutOn(Item* item);
    // Pages 0 / 1 put the item on the hero; the others move it into the page's slots (with Shift and a
    // material the whole stack).
    void PutOnFromStrip(Item* item);
    void RefreshWeapons() override;
    void RefreshStats() override;
    // The hero preview turns while a turn button is held.
    void Update() override;
    // Wrapping; fatal after a full round without a hero (prototype class MapObjectType::Hero).
    void NextHero() override;
    void PreviousHero() override;
    // The unit's own name, else the text of "pers <record name>".
    void ShowHero();
    // Regions 0 / 1 the deals, 2 / 3 turn the hero on the equipment page, 4-11 show page region - 4.
    void LeftDown(int buttons, int x, int y) override;
    void LeftUp(int buttons, int x, int y) override;
    // Esc shows page 7 (leaves); on a network client Backspace and Enter go to the chat line.
    void KeyDown(int key, int repeat, int flags) override;
    const char* HelpTopic() override;
    // By page: 0 repairs the hero's weapons and armour, 1 settles the trade (the money is left alone), 2 / 4
    // a constructor buys its third list, 3 / 5 the made spell / item (or the parts while deconstructing),
    // 6 the repairs. Then the money goes down by cost.
    void DoAction();
    // By page: 1 the trade settled with (0, 0); 2-6 the lists given back.
    void UndoAction();
    // A "Prototype" item for the container's spell and a "Rune" item per modifier.
    void DeconstructSpell();
    // An "Instruction Weapon / Armor / Quick Item", a "material.<key>" per material and for an item with a
    // spell a "spell container" holding a clone of it.
    void DeconstructItem();

    // The console's "give <player> assortment": every instruction, material and rune into the constructors'
    // first lists.
    void DebugFill() override;

    int page = 0;
    bool fromHeroScreen;             // opened from the multiplayer hero screen (closes with a pending value)
    bool spellConstructor = false;   // the spell constructor's pages are offered
    bool itemConstructor = false;    // the item constructor's pages are offered
    ClassTemplate* constructor = nullptr;    // the constructor's class template ("constr.current"), or null
    // A unit per party record, null for other classes.
    std::vector<CUnit*> heroes;
    int hero = -1;                   // the hero shown
    // The constructors' parts and what they made.
    CParty* itemParts[3] = {};
    CParty* itemResults = nullptr;
    CParty* itemExtra = nullptr;
    CParty* spellParts[3] = {};
    CParty* spellResults = nullptr;
    HeroStatsPanel* statsPanel = nullptr;
    ItemInfoPanel* helpPanel = nullptr;
    UiItemStrip* itemStrip = nullptr;
    HeroEquipPanel* equipment = nullptr;
    HeroQuickStrip* equipment2 = nullptr;
    UiItemStrip* spellStrip = nullptr;
    UiItemStrip* partStrip = nullptr;
    TradePanel* trade = nullptr;
    TradeGrid* spellConstructor1 = nullptr;
    SpellMakerPage* spellConstructor2 = nullptr;
    TradeGrid* itemConstructor1 = nullptr;
    ConstructorPage* itemConstructor2 = nullptr;
    RepairGrid* itemConstructor3 = nullptr;
    int cost = 0;                    // what the action costs (negative: earns)
    bool actionEnabled = false;      // the action button can be pressed
    bool actionShown = false;
    bool deconstructing = false;     // the constructor takes an item apart
    Ref<Item> itemMade;             // the item the item constructor would make, or null
    Ref<Item> spellMade;            // the spell container the spell constructor would make, or null
    float priceWeights[9] = {};      // ClassTemplate::weights
    std::vector<std::string> pageTitles;    // the seven pages' titles and "quit"
    bool noConstructor = false;      // the player's "i.noconstr" = 1: second pages hidden
    float heroTurn = 0.0f;           // the hero preview's turn (radians)
    int turning = 0;                 // 1 / 2 while a turn button is held (Update turns the hero)
};

// --- Hero statistics panel ---
// The statistics column of the camp and the multiplayer hero screen.
class HeroStatsPanel : public UiItemPanel {
public:
    using UiItemPanel::UiItemPanel;
    // Page 0 the body parts and the weapon, 10 the new hero's points with -/+ buttons, others the plain
    // values.
    void ShowStats(int page);
    void DrawStats();
    void Build() override;
    // Page 0: rows 2-5 the body parts' armour, 6 / 7 the shown weapon's attack / defence, 8 the damage, 9
    // encumbrance, 10 the magic skills; other pages the attributes (page 10: row 14 the points left, row 5
    // the height).
    ItemHoverInfo HoverInfo(int x, int y);
    // `title` may be null; the hero arrows show only for a party of more than one; points = the hero's
    // height * 100 (truncated).
    void SetHero(int heroIndex, std::optional<std::string_view> title);
    // On page 10 a -/+ button (regions 6-13) steps a point and is held; elsewhere region 0 / 1 show the
    // owner's previous / next hero.
    void LeftDown(int buttons, int x, int y) override;
    // A held -/+ button repeats every frame after a quarter of a second until it can step no more; the owner
    // is refreshed on every attempt.
    void Update() override;
    // Lets the mouse go unconditionally and ends the hold.
    void LeftUp(int buttons, int x, int y) override;
    // Regions 6-8 take a point from strength, dexterity, intelligence (above 15), 9 the height down
    // (points >= 1), 10-12 give one (below 35, while the three add up to under 75), 13 the height up
    // (points <= 99; height = points * 0.01). True when it changed something.
    bool StepPoint(int region);
    // The text of "perk <label>" (else `label` itself), `value` as "%d", or "%d/%d" with a maximum >= 0.
    void DrawRow(const RECT& rect, std::string_view label, int value, int maximum);
    // "     " and the text of "hl_<name>", the part's armour as "%.1f".
    void DrawPart(const RECT& rect, std::string_view name, int part);
    // Per damage type the three layers' absorption summed (stored as float); the largest of the first six.
    float PartArmour(int part);

    int heroIndex = 0;
    std::string title;
    int page = 0;
    std::vector<std::string> texts;
    int points = 0;                  // skill points left (page 10); SetHero puts the height * 100 here
    int heldRegion = -1;             // the -/+ button held on page 10 (-1 none)
    float holdTime = 0.0f;           // seconds it has been held (from -0.5)
};

// --- Hero equipment panel ---
// The eight quick slots along the top, and in the camp the body with its seven equipment slots.
class HeroEquipPanel : public UiItemPanel {
public:
    using UiItemPanel::UiItemPanel;
    void Draw(SceneRenderer* pass) override;
    // With `body` (the camp) the panel is 500 high with the seven body slots.
    void Build(bool body);
    // The place in the hero's weapon list, or -1.
    void ShowWeapon(Item* weapon);
    // The place in the hero's quick item list, or -1.
    void ShowQuickItem(Item* item);
    // On the camp's body: a right click on a filled weapon slot (of the first four) shows that weapon. The
    // event is not marked used.
    void RightDown(int buttons, int x, int y) override;
    // Regions 0-3 the weapons (topic 2), 4-7 the quick items from the right (3), body regions 8-14 the armour
    // worn at body part region - 8 (topic region - 4).
    Item* ItemAt(int x, int y, int* topic) override;

    bool body = false;
    int selected = -1;
};

// The 4 x 4 grid of item cells on the constructors' first pages.
class TradeGrid : public UiItemPanel {
public:
    using UiItemPanel::UiItemPanel;
    void Draw(SceneRenderer* pass) override;
    // `spells` chooses the price kinds (0/1 for the spell constructor, 4/5 for the item one); the
    // exchange window's grid (`exchange`) shows no prices.
    void Build(std::vector<Item*>* bought, std::vector<Item*>* sold, bool spells, bool exchange);
    // Regions 0-7 the bought list (topic 0x1C on the exchange window, else 0x0B for the item constructor,
    // 0x11 for the spell one), 8-15 the sold list (0x1D / 0x0C / 0x12).
    Item* ItemAt(int x, int y, int* topic) override;

    std::vector<Item*>* bought = nullptr;
    std::vector<Item*>* sold = nullptr;
    int buyKind = 0;
    int sellKind = 0;
    bool exchange = false;
};

// The repair page's grid: the same cells as the trade grid with textures repair1..4.
class RepairGrid : public UiItemPanel {
public:
    using UiItemPanel::UiItemPanel;
    void Build(std::vector<Item*>* items);
    // Each item's model in its cell (depth 2^(3 - row) * 6), its count and its repair price.
    void Draw(SceneRenderer* pass) override;
    // Topic 0x19 on the 16 cells.
    Item* ItemAt(int x, int y, int* topic) override;

    std::vector<Item*>* items = nullptr;
};

// The quick item strip of the hero panels: eight cells along the top (camp) or the bottom of the screen.
class HeroQuickStrip : public UiItemPanel {
public:
    using UiItemPanel::UiItemPanel;
    // At y 0 (`top`) or 500.
    void Build(bool top);
    // The place in the hero's spell list, or -1.
    void ShowEntry(Item* item);
    // The hero's spell list entry (topic 1).
    Item* ItemAt(int x, int y, int* topic) override;
    void Draw(SceneRenderer* pass) override;

    int y = 0;
    int selected = -1;
};

// The constructors' second pages: the spell maker and the item maker: the slots of what is being made beside
// a 2 x 4 grid.
class ConstructorPage : public UiItemPanel {
public:
    using UiItemPanel::UiItemPanel;
    void BuildItemMaker(CParty* slots);
    // Without a blueprint or a container, a pulsing highlight shows where the next part goes.
    void Draw(SceneRenderer* pass) override;
    // Region 0 the first item that can be taken apart (0xD), 1 the first part (0xE), 2 the first spell
    // container (0x10), from 3 material region - 3 (0xF).
    Item* ItemAt(int x, int y, int* topic) override;

    CParty* slots = nullptr;
    float pulse = 0.0f;              // the highlight's time
};
class SpellMakerPage : public ConstructorPage {
public:
    using ConstructorPage::ConstructorPage;
    // Region 0 the first spell container (topic 0x13), 1 the first spell (0x14), 2-9 modifier region - 2
    // (0x15), 10 / 11 the cost lines (0x1A / 0x1B, no item).
    Item* ItemAt(int x, int y, int* topic) override;
    void BuildSpellMaker(CParty* slots);
    // The cost lines: "<text 0> <best skill> (<needed>)" and "<text 1> <most mana> (<the larger of the summed
    // and the last spell's mana>)" with a spell. Without a container, a pulsing highlight shows where the
    // next part goes.
    void Draw(SceneRenderer* pass) override;

    std::vector<std::string> texts; // the cost texts ("infoitem_35", "infoitem_36")
};
