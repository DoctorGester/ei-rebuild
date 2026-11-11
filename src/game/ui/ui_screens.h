#pragma once

// Interface screens (docs/notes/ui.md, "Screens"): the camp's item strips, the multiplayer hero and
// connection screens, the escape menu, the main menu and the exchange window (the credits: ui_credits.h). Handlers take
// a UiEvent (ui.h).

#include "game/ui/ui_item_info.h"
#include "game/ui/ui_message_box.h"
#include "game/ui/ui_drop_list.h"
#include "engine/mods/mod_list.h"
#include "game/net/net_session.h"
#include "game/spells/spell_db.h"

#include "game/ui/screen_layout.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class UiScrollbar;
class UiSlider;

class HeroEquipPanel;
class HeroQuickStrip;
class HeroStatsPanel;
class TradePanel;

namespace render::text {
struct RichLayout;
} // namespace render::text

// The item predicates of the camp screens. Loot items (type ItemKind::Loot, ItemLoot) are sorted by kind and record
// type; the four loot predicates hold only for a loot item whose record exists (ItemLoot::HasRecord), so the code
// behind them reads the record unchecked.
bool IsPart(const Item* item);             // kind 2, record type 0-2 (blueprint parts)
bool IsMaterial(const Item* item);
bool IsSpell(const Item* item);            // kind 2, record type 3 (the spell table)
bool IsModifier(const Item* item);         // kind 2, record type 4 (spell modifier table)
bool IsSpellContainer(const Item* item);   // type ItemKind::Scroll
// An item the item constructor can take apart: a weapon or armour, or a quick item whose prototype's material
// class is not "none"; never when the prototype exists and is not buildable.
bool IsBuildable(const Item* item);

// --- Camp item strip ------------------------------------------------------------------------
// The horizontal strip of items (or spells) at the bottom (y 500) or top (y 0) of the camp screens: six tab regions
// choose a category, two arrows scroll it, the strip itself shows `visible` slots. Created by the camp screen and
// the exchange window.

// An item panel (UiItemPanel): mode is what clicking an item does (1 equip, 2, 3, cursor 0x16-0x18); owner is the
// screen that owns the strip (item clicks, predicates).
class UiItemStrip : public UiItemPanel {
public:
    // The plain strip (the visible-list virtual is pure), the base of the forwarding and the figure strips.
    using UiItemPanel::UiItemPanel;
    virtual void RebuildList() = 0;

    // For the camp's three strips and the exchange window's: the background texture "Inventory02" (`top`) or "Inventory01", the
    // strip built, the arrows' tips 0x4FB0 / 0x4FB1, two text surfaces (x 50..750, y top + 15..35 and top + 65..85) and the
    // source list.
    void Setup(std::vector<Item*>* source, bool top);
    // The text surfaces' paints, over the items in view: the stack counts (not 1); unless priceKind is None, the
    // owner's number (Price) of each item the strip's mode takes (CanEquip).
    void PaintCounts(UiSurface& surface) const;
    void PaintPrices(UiSurface& surface) const;
    // Lays the strip out along the top edge (`top`: y 0, scale 8) or the bottom (y 500, scale 4): the scroll state cleared, nine
    // regions (arrows, six tab buttons, the items), the ends' and tab buttons' sprites, the highlight frame, the background
    // cells; tab 5.
    void Build(bool top);
    // Selects tab `tab`: settles the current tab to a whole slot, highlights its button, stops scrolling.
    void SetTab(int tab);
    // Chooses what the tabs sort by (0 items, 1 spells, 2 armour), loads their tooltips and resets every tab's position.
    void SetFilter(int filter);
    // Tab selection used by the camp screen while tabs are enabled: select `tab`, or cycle within [first, first + count).
    void SelectTab(int tab);
    void CycleTab(int first, int count);
    // The tab (0-5) an item belongs to under each filter.
    int ItemTab(const Item* item);
    int SpellTab(const Item* item);
    int ArmorTab(const Item* item);
    // Rebuilds the list as RebuildList does, first switching to `item`'s tab (SetTab, while the tab keys are enabled; not for
    // filter 0's tab 5), and scrolls the tab so the last entry the same as `item` is visible: its position moves into
    // [index - 6, index]. The scroll stops; count is not updated. Null does nothing.
    void ScrollToItem(Item* item);
    // The tab of `item` under the current filter (0 for an unknown filter), as both rebuilds choose it.
    int TabOf(const Item* item);
    // Every frame. Scrolls: a held arrow adds one slot to the pending scroll at once and one more every 0.1 s; the pending
    // scroll moves the current tab's position at an accelerating speed (15 slots/s^2, at most 7), easing out within half a
    // slot and settling on a whole slot; then the list is rebuilt and the position kept within [0, count - 7].
    void Update() override;

    void OnEvent(UiEvent& event) override;

    void LeftDown(UiEvent& event);
    void LeftUp(UiEvent& event);
    void MouseMove(UiEvent& event);
    void Wheel(UiEvent& event);

    // Settles the pending scroll on release.
    void SettleScroll();
    // The list index under the scaled pixel column `x`: 100-pixel cells from x 50 plus the current tab's position snapped
    // once to a whole pixel (its floor, so the sum truncates as the original's (x - 50) * 0.01 + position did, but for
    // an exact cell edge). Not range-checked.
    int SlotAt(int x) const;

    int tab = 0;                // 0-5
    int scrollDirection = 0;    // 0 none, 1 left arrow held, 2 right arrow held
    int scrollTicks = 0;        // milliseconds into the held arrow's repeat (0 not held yet)
    float position[6] = {};     // scroll position of each tab, in slots
    float pending = 0.0f;       // scroll still to apply, in slots
    float velocity = 0.0f;
    int selected = 0;           // -1; the hero screen's figure strips: the chosen figure
    int top = 0;                // y of the strip: 500 (bottom) or 0 (top)
    float scale = 0.0f;
    int count = 0;              // visible list size after the last rebuild
    bool tabsEnabled = false;
    int hovered = 0;            // -1
    int filter = 0;             // 0 items, 1 spells, 2 armour
    std::vector<Item*> list;           // the visible items
    std::vector<Item*>* source = nullptr; // all the items (a party's item list, PartyItemArray)
    PriceKind priceKind = {};   // the price shown under each item (None: no price)
    static constexpr int kTabs = 6;
    static constexpr int kAllTab = 5;   // under filter 0 every item
    static constexpr int kCells = 8;
    UiRegion* scrollButtons[2] = {};    // back and on
    UiRegion* tabButtons[kTabs] = {};   // three at the left end, three at the right
    UiRegion* itemArea = nullptr;
    UiMesh* endFaces[2] = {};           // dimmed while the strip can scroll that way
    UiMesh* tabFaces[kTabs] = {};
    UiMesh* highlightFrame = nullptr;
    UiMesh* background = nullptr;
    UiMesh* cellFrames[kCells] = {};
};

// The forwarding strip: the camp's item strips and the exchange's; an item click goes to the owner.
class ForwardingStrip : public UiItemStrip {
public:
    using UiItemStrip::UiItemStrip;
    // The end faces dimmed (sprite alpha 0.195) where it can scroll on, the background cells shifted by the current tab's
    // fractional position, the window's sprites, eight item cells through the owner (its two text surfaces paint
    // themselves: UiItemStrip::PaintCounts, PaintPrices).
    void DrawSprites(SceneRenderer* pass) override;
    // On the item cells: the item under the screen point, or null, with the help topic of the strip's action mode.
    ItemUnder ItemAt(Point point) override;
    // Rebuilds the visible list from the source list for the current filter and tab (tab 5 of filter 0 shows everything).
    void RebuildList() override;
    void OnEvent(UiEvent& event) override;
    // The strip's own click, then the item under the point to the owner.
    void LeftDown(UiEvent& event);
};

// The figure strips (the hero screen's two, the exchange window's).
class FigureStrip : public UiItemStrip {
public:
    using UiItemStrip::UiItemStrip;
    // The figure strips (the hero screen's two, the exchange window's): the name list, the texture "InventoryM01", Build, the
    // arrows' tips 0x9DD0 / 0x9DD1 unless `noArrowTips`, no tabs, the tab buttons' sprites hidden.
    void SetupFigures(std::vector<std::string>* names, bool top, bool noArrowTips);
    // The end faces dimmed where it can scroll on, the background and cells placed by the current tab's fraction, the
    // highlight frame on selected when in view, the window's sprites, then each visible name's hero face through the owner.
    void DrawSprites(SceneRenderer* pass) override;
    // Count_ = the number of names.
    void RebuildList() override;
    void OnEvent(UiEvent& event) override;
    // The strip's own click, then on the items the figure under the point picked: the "on_off" sound and the
    // owner's choose-figure virtual with its name.
    void LeftDown(UiEvent& event);

    std::vector<std::string>* figures = nullptr;    // the figure names
};

// --- Camp screen ------------------------------------------------------------------------------

// A party hero's statistics as the camp reads them: the skill in a spell school rounded to nearest, and the
// whole "mana units" (the mana maximum stored as float, divided by the period, truncated).
int HeroSchoolSkill(UnitStats* stats, SpellSchool school);
int HeroManaUnits(UnitStats* stats);
// Whether a hero with `stats` can use the spell `spell` (required skill, mana cost).
bool HeroCanUseSpell(UnitStats* stats, Spell* spell);

// --- Multiplayer hero screen ----------------------------------------------------------------

// A voice of the hero screen.
struct MpVoiceEntry {
    std::string file;
    std::string name;
};
// Opened by the main menu's Multiplayer button (region 5). Modes: 9 hero list, 10 new hero (portrait, name and voice),
// 11 new hero's statistics. The heroes are records in a global vector.
class MpHeroScreen : public ItemScreen {
public:
    // The modes (mode; the statistics panel's page too).
    static constexpr int kModeList = 9;
    static constexpr int kModeNewHero = 10;
    static constexpr int kModeStats = 11;
    // The points a new hero's three attributes must add up to before it can be created (a float in the exe's data).
    static constexpr float kAttributePoints = 75.0f;
    // The rows of the hero list and of the voices.
    static constexpr int kHeroRows = 10;
    static constexpr int kVoiceRows = 11;
    // The heroes saved, the player's records handed to the client world, the preview cleared and the description panel emptied.
    void CloseScreen() override;
    // The curtains over the margins of the window's new shape.
    void LayoutChanged() override;
    // With a preview figure: the hero's statistics restored, its size factors, scale and unit statistics; then the statistics
    // panel redrawn.
    void RefreshStats() override;
    // The local player's first hero.
    PartyUnitRecord* HeroRecord() override;
    // The weight carried by the hero being made (weapon, armour, spell container and the quick items).
    float Weight();
    // Keeps the selected hero in the list and visible (ten rows), then shows it.
    void ClampHeroSelection();
    // The same for the voice list (eleven rows), then shows the voice's name.
    void ClampVoiceSelection();
    // What an item strip calls with its action mode: with mode 0, equips a weapon, armour or spell container; then refreshes.
    void TakeItem(Item* item, int mode) override;
    // Whether the hero can take `item`; spell containers need the spell's skill and mana.
    bool CanEquip(Item* item, int mode) override;

    void OnEvent(UiEvent& event) override;

    void LeftUp(UiEvent& event);
    // Nothing while a hero is being renamed; after the base handler, clicks by mode (9 list, 10 new hero, 11 statistics):
    // see docs/notes/ui.md, "MpHeroScreen::LeftDown".
    void LeftDown(UiEvent& event);
    // After the container's handler, unless a child used the key: Enter, Esc and the paging keys by mode (9, 10, 11):
    // see docs/notes/ui.md, "MpHeroScreen::KeyDown". A key used is marked.
    void KeyDown(UiEvent& event);
    // Enter while renaming: the hero's name becomes the text before its " | " plus " | " and the typed text (trimmed); the
    // transfer sent; the edit window put away.
    void FinishRename();
    // The rename edit window put away (its surface hidden, disabled, editing off).
    void CloseRenameEdit();
    // Back from the statistics page to the new hero (mode 10).
    void BackToNewHero();
    // The hero's first weapon, armour (its first quick item) and spell container chosen and shown.
    void ShowFirstEquipment();

    // Shows hero `index` (none when not in the list): copied into the local party and made current, the preview rebuilt, the
    // unit re-armed, the turn reset and the list redrawn.
    void ShowHero(int index);
    // The preview figure of the local player's first record (`allArmour` as GameCreatePartyUnits takes it): see
    // docs/notes/ui.md, "MpHeroScreen::MakePreview".
    void MakePreview(bool allArmour);
    // The new hero takes figure (unit prototype) `prototype`: see docs/notes/ui.md, "MpHeroScreen::ChooseFigure".
    void ChooseFigure(std::string_view prototype);
    // The voices' surface (2): the "lmp_voices" title and eleven rows from voiceTop, the selection frame on the chosen one.
    void DrawVoices();
    // The hero list's surface (0): ten rows of names from heroTop, the selection frame on the selected hero.
    void DrawHeroList();

    // On the statistics page the experience lines on surface 2; then what is under the mouse for the description panel, the
    // children drawn and the hero previewed (list at depth 553, new hero at 400).
    void DrawSprites(SceneRenderer* pass) override;
    // The children's update; a held rotate arrow turns the preview by twice the frame time; the scroll bars' moves redraw the
    // list and voices; on the new hero page the create button is bright only when the three attributes add up to the
    // attribute points and the name is not empty.
    void Update() override;
    void DrawExperience();
    // The list's rename button: the rename edit window enabled and editing; its text is the selected hero's name after
    // its "|", surface 3 shows the name before it plus " | ". Nothing without a selection.
    void Rename();
    // The hero's voice: the sound playing stopped, then one of the voice's sound-set entries with flags 0x2282 and a name, from
    // a random one on, played (voiceSound).
    void PlayVoice();
    // Enter / the list's forward button: with a hero selected the heroes are saved and the connection screen opened;
    // otherwise a message box.
    void Continue();
    // The forward button / Enter of the new hero: when the three attributes add up to the attribute points and a name
    // is typed, the first weapon, one more entry and the spell entry shown, the name given to the hero and the
    // statistics panel, mode 11; otherwise the cancel sound.
    void CreateHero();
    // The selected hero deleted: its file removed, later records moved down, the selection kept, the list's scroll range and
    // list redrawn. Nothing without a selection.
    void DeleteHero();
    // A message box answered: 0x10001 (the "lmp_deletepers" OK) deletes the hero; then the selected hero's record takes the
    // local player's party and sends the transfer; mode 9.
    void MessageResult(std::uint32_t result) override;
    // The forward button / Enter of the statistics page: the new hero is kept and saved as a record: see
    // docs/notes/ui.md, "MpHeroScreen::AddNewHero".
    void AddNewHero();
    // The screen laid out for `mode` (9 list, 10 new hero, 11 statistics): see docs/notes/ui.md, "MpHeroScreen::SetMode".
    // Rebirth's WideScreen shows the curtains in modes 10 and 11 and hides them in 9.
    void SetMode(int mode);
    // The screen's Create (reproduces Rebirth's WideScreen: the two curtains over the margins, after its sprites): see
    // docs/notes/ui.md, "MpHeroScreen::CreateScreen".
    void CreateScreen() override;
    // The hero list's scroll bar after the count changed (DeleteHero and AddNewHero inline it):
    // range count - 10 keeping the position, or 0 with the top row 0.
    void RefreshHeroScroll();
    // The shown list's row of `region` (-1 for a button or none).
    int ListRow(const UiRegion* region) const { return RegionIndex(listRows, region); }

    int mode = 0;               // 9, 10, 11
    int heroIndex = 0;          // selected hero
    UiScrollbar* heroScroll = nullptr;  // scroll bar of the hero list
    int heroTop = 0;            // first row shown
    Unit* previewModel = nullptr;  // the hero's figure shown by the preview
    Item* weapon = nullptr;
    Item* quickItem = nullptr;
    Item* spellContainer = nullptr;
    HeroStatsPanel* statsPanel = nullptr;
    HeroEquipPanel* weaponPanel = nullptr;  // shows the weapon and the quick item
    HeroQuickStrip* spellPanel = nullptr;
    FigureStrip* figureStrips[2] = {}; // the figures of defaults (voices voiceLists[0]) and of names (voiceLists[1])
    TradePanel* tradePanel = nullptr;
    std::vector<std::string> defaults;    // the men's figures; its first entry goes to ChooseFigure for a new hero
    std::vector<std::string> names;       // the women's figures
    float turn = 0.0f;          // the preview's turn
    int rotating = 0;           // 0, 1 or 2 while a rotate arrow is held
    UiEditField* renameEdit = nullptr;  // the hero list's rename edit window (focused while renaming)
    UiEditField* nameEdit = nullptr;  // the new hero's name edit window
    UiScrollbar* voiceScroll = nullptr;
    std::string text;
    std::vector<MpVoiceEntry> voiceLists[2];    // voices points at one of them
    std::vector<MpVoiceEntry>* voices = nullptr;  // voiceLists[0] or [1]
    int voiceTop = 0;
    int voiceIndex = 0;
    int voiceSound = 0;
    // The four surfaces' paints: 0 the hero list, 1 the list's commands, 2 the voices or the experience lines, 3 the
    // renamed hero's name.
    std::vector<UiLabel> labels[4];

    // The regions every mode keeps, in their order (SetMode places the first four and gives the tips).
    UiRegion* backButton = nullptr;        // the list: the main menu; the new hero: cancel; its statistics: back
    UiRegion* forwardButton = nullptr;     // the list: continue; the new hero: create; its statistics: add the hero
    UiRegion* turnButtons[2] = {};         // the preview turned one way (rotating 1) or the other (2)
    UiRegion* resetButton = nullptr;       // the new hero's defaults; its statistics: the equipment taken back
    UiRegion* newHeroButton = nullptr;     // the list's four commands
    UiRegion* renameButton = nullptr;
    UiRegion* deleteButton = nullptr;
    UiRegion* inventoryButton = nullptr;
    std::vector<UiRegion*> listRows;       // the shown list's rows, made anew by SetMode
    screen_layout::Curtains curtains;      // over the screen's margins, after the screen's sprites
    // The sprites SetMode shows and places by mode.
    UiMesh* selectionFrame = nullptr;      // on the selected hero or voice
    UiMesh* quarters[4] = {};              // the frame around the figure
    UiMesh* resetIcon = nullptr;           // the new hero's buttons; createIcon lit when the hero can be made
    UiMesh* cancelIcon = nullptr;
    UiMesh* createIcon = nullptr;
};

// --- Multiplayer connection screen --------------------------------------------------------------
// The servers found (config\addrbook.ini keeps the ones asked directly): a list of 15 rows with a scroll bar, row 0
// the entry that hosts a new game. Regions: 0 connect / host, 1 back, 2 ask the master server, 3 add / forget an
// address, 4 the unlisted switch, 5 the password, 6-9 the sort columns, 10-13 the game type tabs, 14-28 the list
// rows, 29 the pause mode (ours). Sprites: 4 the unlisted switch, 5 the password's, 6-9 the game type tabs
// (textureOffsetU 0.09 selected).

// An entry of the connection screen's list.
struct MpAddressEntry {
    std::string name;             // the server's name
    std::string description;      // the current map's title
    int players = 0;
    int maxPlayers = 0;
    int ping = 0;                  // milliseconds (9999: none)
    net::NetAddress address;
    int gameType = 0;              // 0..3 (4: the new game entry)
    bool foreign = false;          // the server answered with another protocol
    int state = 0;                 // net::ServerEntryBase::State (1: asked directly, forgettable)
    bool password = false;         // joining needs a password
    // Ours: the host's mod from its info answer (net::ServerEntry::mod), shown in the details once an answer was read
    // (`modRead`); a host this game cannot join for the mods (`joinable` false) is drawn dimmed.
    bool modRead = false;
    std::optional<modidentity::Identity> mod;   // none: the original game
    bool joinable = true;
};

// The colour the connect list's texts and its line are drawn in: set per row and for the details by the list draw
// and left as it is for the next draw's titles; Create's texts take it too.
inline std::uint32_t g_connectTextColour = /*00793AD8*/ 0xA7D7E4;

class MpConnectScreen : public UiContainer {
public:
    using UiContainer::UiContainer;
    void CloseScreen() override {}

    // The questions the screen opens message boxes with; MessageResult gets BoxResult(question, answer) back.
    static constexpr MessageQuestion kBoxNotice = MessageQuestion::First;           // a message (refusal, wrong protocol)
    static constexpr MessageQuestion kBoxAddress = MessageQuestion::Second;         // an address to ask
    static constexpr MessageQuestion kBoxPassword = MessageQuestion::Third;         // the new game's password
    static constexpr MessageQuestion kBoxMaster = MessageQuestion::Fourth;          // the master server request (MasterRequestBox)
    static constexpr MessageQuestion kBoxJoinPassword = MessageQuestion::Fifth;     // the password of the server joined
    static constexpr int kRows = 15;

    // The screen over 800 x 600 (the vanilla body; Rebirth patches it): see docs/notes/ui.md,
    // "MpConnectScreen::CreateScreen".
    void CreateScreen() override;
    // Surface 1 ("connecting") shown while the client asks a host; when it stops asking, a refusal box ("lmp_server_full",
    // "..._you_are_banned", "..._wrong_password", else "..._cannot_connect"; text key + "_msg") and nothing else this frame.
    // Otherwise the children, and every 0.5 s the client asks the selected address (none for row 0) and the list is rebuilt
    // and drawn; the scroll bar, the player slider (maxPlayers = its position + 1) and a changed name redraw the list.
    void Update() override;
    // A box answered: an address (0x20001) is parsed (port 8888 unless given) and asked when known, the
    // address book written, the list drawn; the master request ending (0x40002) draws the list; the new
    // game's password (0x30001 / 0x30002) is kept and its switch shows whether there is one; a join password
    // (0x50001) joins the selected server with it and sets the client's game type from the row.
    void MessageResult(std::uint32_t result) override;
    void OnEvent(UiEvent& event) override;
    // By region: Connect, back (the cancel sound, the address book written, the network sessions dropped, the screen
    // closed with 0x1000001), MasterRequest, EditAddress, the unlisted switch, the password (none: a box for one; else it
    // is dropped), a sort column (a second click reverses it), a game type tab or the pause mode's next choice (only
    // with row 0 selected), a row (the select sound, it and its address selected); then the container's handler.
    void LeftDown(UiEvent& event);
    // A row double-clicked is selected (no sound, no drawing) and Connect follows; then the container's handler.
    void LeftDouble(UiEvent& event);
    // The container's handler first; then Enter connects, Esc leaves (as back without the address book), Up / Down / PgUp /
    // PgDn move the selection by 1 / 15 with the select sound (SelectAndScroll); those keys are marked used.
    void KeyDown(UiEvent& event);
    // The selection clamped to the list (an empty list gives -1 and reads the entry before it), its address kept, the list
    // scrolled to show it and drawn.
    void SelectAndScroll();
    // The OK sound; row 0 hosts (GameFlow::StartServer: the name typed or the default, the player slider + 1,
    // pauseMode, gameType, hostPassword, unlisted, the client's queries' extra word) after the address book is
    // written; another row joins: a foreign server gets the "lmp_wrong_protocol" box; otherwise the address book is
    // written, a password server opens the password box, the server is joined at once with no password and the
    // client's game type and player limit set from the row.
    void Connect();
    // On a valid selection: an entry asked directly is forgotten (and the address book written); another
    // opens the address box (texts[19]) with the row's address (empty for row 0).
    void EditAddress();
    // The master server request: the master address (a text in the game's data, port 28004) in a
    // MasterRequestBox (texts[20]) with the client's queries' extra word (EI_Plugin's NewMaster with a
    // second master set: the master tried first).
    void MasterRequest();
    // Writes the client's queries to config\addrbook.ini (a chunk archive, chunk 1).
    void SaveAddressBook();
    // The list's controls, sprites and tips for the selection, and the text colour left as the list's drawing left
    // it (docs/notes/ui.md, "MpConnectScreen::DrawListVanilla"); PaintList, surface 0's paint, draws the list's texts
    // (Rebirth-patched: WideScreen, and ExtendedServerName behind its flag).
    void DrawListVanilla();
    void PaintList(UiSurface& surface) const;
    // The list again: the client's servers in order, sorted by sortKey (std::sort), the new game entry
    // inserted first; the selection follows its address (row 0 and address 0 when gone); the scroll bar's range is
    // the rows past 15 and its position set again; scrollTop 0.
    void RebuildList();

    static constexpr int kSortColumns = 4;
    static constexpr int kTabs = 4;
    UiRegion* connectButton = nullptr;
    UiRegion* backButton = nullptr;
    UiRegion* masterButton = nullptr;
    UiRegion* addressButton = nullptr;
    UiRegion* unlistedSwitch = nullptr;
    UiRegion* passwordSwitch = nullptr;
    UiRegion* sortColumns[kSortColumns] = {};
    UiRegion* tabAreas[kTabs] = {};          // the new game's type tabs
    UiRegion* pauseSwitch = nullptr;         // the new game's pause mode: a click steps to the next
    UiRegion* companionSwitch = nullptr;     // ours: the new game's companions, a click turns them on or off
    UiRegion* rows[kRows] = {};              // the visible rows, from scrollTop
    UiMesh* selectionMarker = nullptr;
    UiMesh* sortArrow = nullptr;
    UiMesh* unlistedBox = nullptr;           // the switches' check boxes
    UiMesh* passwordBox = nullptr;
    UiMesh* tabBoxes[kTabs] = {};
    float refreshTimer = 0.0f;     // seconds since the last refresh (0.5 s period)
    std::vector<MpAddressEntry> entries;
    int selected = 0;              // row
    net::NetAddress selectedAddress{};    // the selected row's address (0: row 0)
    int scrollTop = 0;             // the first row shown
    UiScrollbar* scroll = nullptr;
    UiSlider* playersSlider = nullptr;  // the new game's maximum players - 1
    UiEditField* nameEdit = nullptr;  // the new game's name
    int maxPlayers = 0;            // the slider's position + 1 (6 from Create)
    int gameType = 0;              // the new game's type tab 0..3
    net::PauseMode pauseMode{};    // the new game's pause mode (the original's from Create)
    int companions = 0;            // ours: the companions a player may add in the new game, 0 none (Create)
    char boxText[0x80] = {};       // the edit boxes' text (an address, a password)
    bool unlisted = false;         // the new game is not announced (1 from Create)
    std::string hostPassword;     // the new game's password
    std::vector<std::string> allodNames; // "lmp_allod_name %d" texts (the game type tabs)
    bool joining = false;          // the client was asking a host last frame
    std::vector<std::string> texts;       // "lmp_connection %d" texts (22)
    int sortKey = 0;               // 0 none; 1..8 the column pairs (odd ascending)
    bool newGameTips = false;      // the unlisted, password and pause regions have their tips (DrawList; 0 from Create)
    std::uint32_t titleColour = kUiTextColour;   // the titles' colour: g_connectTextColour before the last DrawListVanilla
};

// A message box waiting on a master server request: one cancel button, the link to the master server and a progress bar
// sprite. Its result is kBoxMaster (| 2 when done).
class MasterRequestBox : public MessageWindow {
public:
    // The box (one cancel button), the link connecting to `address`, the
    // bar (a plain sprite, colour 0xFFA06800), and `extra` written to the link's outgoing buffer.
    MasterRequestBox(std::string_view titleKey, std::string_view textKey, MessageQuestion question,
                      const net::NetAddress* address, std::uint32_t extra);
    // The bar's right edge at 225 + 355 * the received share; the link steps: done (3) hands the reply to the
    // client's server list; the box closes (0x40002) when the link is done or, stepping again, closed (up to
    // three steps a frame).
    void Update() override;

    net::MasterLink link;
    UiMesh* bar = nullptr;
};

// --- In-game escape menu ----------------------------------------------------------------------
// Opened by Esc during a game: five stones over the stone menu model.
class EscMenu : public UiContainer {
public:
    void OnEvent(UiEvent& event) override;
    void MouseMove(UiEvent& event);
    void KeyDown(UiEvent& event);
    void MessageResult(std::uint32_t result) override;
    // The stone model and the five regions, whose rectangles come from menus.reg.
    void CreateScreen() override;
    // Opens the save window, the load window (neither in multiplayer), resumes, asks to quit or opens the options.
    void LeftDown(UiEvent& event);
    // The menu closes: its stone model (models[0]) plays animation 0.
    void CloseScreen() override;

    // The stones, in their creation order.
    static constexpr int kSave = 0, kLoad = 1, kResume = 2, kQuit = 3, kOptions = 4, kStones = 5;
    UiRegion* stones[kStones] = {};
    int hovered = 0;            // the stone under the mouse, -1 for none
};

// --- Main menu ----------------------------------------------------------------------------------
// The main menu (CreateScreen id 2). Its buttons are the menus.reg "MainMenu" rectangles; the stones are the object
// manager's object 2; the world behind it runs the "MainMenu.cam" camera path with rain at night.
class MainMenu : public UiContainer {
public:
    void DrawSprites(SceneRenderer* pass) override;
    // The map edge fade put back, the sounds and the ambience stopped.
    void CloseScreen() override;
    void OnEvent(UiEvent& event) override;
    // By button: new game the difficulty question (a message box), load the save/load window, exit the exit movie and
    // quit, options, the titles (surfaces cleared, opened over the menu), multiplayer (status "reserved_04", the server
    // flag, the hero screen). Then the container's handler.
    void LeftDown(UiEvent& event);
    // The interface's saved state cleared, the flow's party step 0,
    // the full-screen rectangle, the version line, the six buttons; the default music, the map edge fade kept
    // and set to 6, rain by a coin toss, daylight between 7 and 21, the rain started before 7; the camera
    // path "MainMenu.cam" and the menu music.
    void CreateScreen() override;
    // A surface (100, 480)-(780, 600) with the "version_name" text, added to the window.
    void AddVersionLine();
    // The children's update; with rain the weather turns on at 0 o'clock and off at 7 (the truncated hour
    // changing); at night before 6 with rain and an option bit, every 100 ms a 1 in 41 chance of a lightning
    // bolt at random; the hour kept.
    void Update() override;
    // The list of mods (a drop-down list at the bottom left, only when starter\Mods holds a mod): `mods` are its entries,
    // the one played shown. A pick asks for a switch of mods (app::RequestModSwitch).
    void AddModList();
    // A message box's answer (the flow's party step 0 and the server flag cleared first): 0x10001 (the quit
    // box's yes) the exit movie and quit, 0x20001 the intro movie and the flow's state 3, 0xCCCCCCCC (the
    // titles closed) the version line again.
    void MessageResult(std::uint32_t result) override;
    // Hovering a button plays its stone's animation (kStoneAnimation; off them the rest pose) with the stone sound, once
    // per button; then the container's handler.
    void MouseMove(UiEvent& event);
    // The container's handler, then Esc asks to quit (a message box "quit" / "quit_msg", flags 0x10000).
    void KeyDown(UiEvent& event);

    // The buttons, in their creation order.
    static constexpr int kNewGame = 0, kLoad = 1, kExit = 2, kOptions = 3, kTitles = 4, kMultiplayer = 5, kButtons = 6;
    UiRegion* buttons[kButtons] = {};
    int hovered = -1;                     // the button under the mouse (-1 none)
    bool rain = false;
    bool daylight = false;                   // the hour was between 7 and 21 when the menu opened
    float hour = 0.0f;                      // the time of day at the last update
    float savedEdgeFade = 0.0f;             // the map edge fade before the menu
    std::vector<modlist::Entry> mods;       // the list's entries
    UiDropList* modList = nullptr;          // null without a mod to choose
    int modShown = 0;                       // the entry the list showed when it was made, or last asked for
};

// Trading between two players (screen 9, Xch_* texts). Multiplayer only.
class TradeGrid;
class ExchangeScreen : public ItemScreen {
public:
    // The description panel describes nothing and the scratch party is deleted.
    void CloseScreen() override;
    // The exchange went through: the local party's items are released, every item of `partyC`, then of
    // `partyB`, returned into it (ReturnAll), its money set to partyB's - partyA's + partyC's, and the client
    // goes back to the world.
    void ExchangeDone(Party* partyB, Party* partyA, Party* partyC);
    // Mode 1 (the strip): the put_on sound, `item` moves from the local party into our offer (MoveItem with
    // limit 8, merging) and the offer is published. Mode 5 (the grid), only for an item of our offer: the
    // put_off sound, the item moves back to the local party, the strip scrolls to what the party now holds and
    // the offer is published. Other modes nothing.
    void TakeItem(Item* item, int mode) override;
    // Every item may move.
    bool CanEquip(Item*, int) override { return true; }
    // Builds the window (texture "campinfo", the accept, take back and leave buttons): see docs/notes/ui.md,
    // "ExchangeScreen::CreateScreen".
    void CreateScreen() override;
    // The item (or help topic) under the mouse from the strip, else the grid, goes to the description panel;
    // the four surfaces get "xch_current_money" with the local party's money and "xch_accept",
    // "xch_money_offer", and while the partner's offer pairs with ours "xch_money_remote" with its money and
    // "xch_remote_header" with "xch_remote_ok" / "_cancel"; the accept / take back buttons are shaded by our
    // offer's acceptance and the third button by the partner's; then the children.
    void DrawSprites(SceneRenderer* pass) override;
    void OnEvent(UiEvent& event) override;
    void KeyDown(UiEvent& event);
    // The children's update; then, when the client's offer is gone or detached, our offer's items go back to
    // the local party and the client returns to the world; else a changed money field (its changed cleared)
    // becomes our offer's money (atoi) and the offer is published.
    void Update() override;
    // The container's handler, then by button: accept (the buy sound and the trade recorded with the local
    // party, when our offer is live and not accepted and its money is within the local party's; else the
    // cancel sound), take the acceptance back (cancel sound and the recorded take-back, when it is
    // accepted), leave (the transit sound and the recorded leave). The event is not marked used.
    void LeftDown(UiEvent& event);

    Party* scratch = nullptr;       // a party of the screen's own (made by Create, deleted by Close)
    Party* partnerOffer = nullptr;  // the client's parties[1]
    Party* ownOffer = nullptr;      // the client's parties[0]
    UiItemStrip* strip = nullptr;   // the local party's items (forwarding strip, mode 1)
    FigureStrip* figures = nullptr;  // the players (figure strip)
    TradeGrid* grid = nullptr;      // the two offers (mode 5)
    UiEditField* moneyField = nullptr;  // the money offered
    UiRegion* acceptButton = nullptr;
    UiRegion* takeBackButton = nullptr;
    UiRegion* leaveButton = nullptr;
    UiMesh* acceptIcon = nullptr;      // the buttons' sprites; partnerIcon lit while the partner accepted
    UiMesh* takeBackIcon = nullptr;
    UiMesh* partnerIcon = nullptr;
    std::vector<std::string> names;            // the figure strip's names
    std::vector<UiLabel> labels[4];            // the four surfaces' paints: what Draw found
};
