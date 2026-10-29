#pragma once

// Interface screens (docs/notes/ui.md, "Screens"): the camp's item strips, the multiplayer hero and
// connection screens, the escape menu, the credits, the main menu and the exchange window. Handler arguments:
// mouse (button flags, x, y) in screen pixels; key down (virtual-key code, repeat count, scan code and
// flags); wheel (key flags, wheel delta, x, y).

#include "bink_video.h"
#include "ui_item_info.h"
#include "net_classes.h"

#include "game_data.h"
#include "resource.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class HeroEquipPanel;
class HeroQuickStrip;
class HeroStatsPanel;
class TradePanel;

namespace render::text {
struct RichLayout;
} // namespace render::text

// The item predicates of the camp screens. Loot items (type ItemKind::Loot, ItemLoot) are sorted by kind and record
// type.
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
// the exchange window. Regions: 0/1 arrows, 2-7 tabs, 8 the strip.

// An item panel (UiItemPanel): mode is what clicking an item does (1 equip, 2, 3, cursor 0x16-0x18); owner is the
// screen that owns the strip (item clicks, predicates).
class UiItemStrip : public UiItemPanel {
public:
    // The plain strip (the visible-list virtual is pure), the base of the forwarding and the figure strips.
    using UiItemPanel::UiItemPanel;
    virtual void RebuildList() = 0;

    // For the camp's three strips and the exchange window's: the background texture "Inventory02" (`top`) or "Inventory01", the
    // strip built, the arrows' tips 0x4FB0 / 0x4FB1, two text surfaces (x 50..750, y top + 15..35 and top + 65..85), nine
    // slots and the source list.
    void Setup(std::vector<Item*>* source, bool top);
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

    void LeftDown(int flags, int x, int y) override;
    void LeftUp(int flags, int x, int y) override;
    void MouseMove(int flags, int x, int y) override;
    void Wheel(int keys, int delta, int x, int y) override;

    // Settles the pending scroll on release.
    void SettleScroll();
    // The list index under the scaled pixel column `x`: 100-pixel cells from x 50 plus the current tab's position snapped
    // once to a whole pixel (its floor, so the sum truncates as the original's (x - 50) * 0.01 + position did, but for
    // an exact cell edge). Not range-checked.
    int SlotAt(int x) const;

    int tab;                    // 0-5
    int scrollDirection;        // 0 none, 1 left arrow held, 2 right arrow held
    int scrollTicks;            // milliseconds into the held arrow's repeat (0 not held yet)
    float position[6];          // scroll position of each tab, in slots
    float pending;              // scroll still to apply, in slots
    float velocity;
    int selected;               // -1; the hero screen's figure strips: the chosen figure
    int top;                    // y of the strip: 500 (bottom) or 0 (top)
    float scale;
    int count;                  // visible list size after the last rebuild
    bool tabsEnabled;
    int hovered;                // -1
    int filter;                 // 0 items, 1 spells, 2 armour
    std::vector<Item*> list;           // the visible items
    std::vector<Item*>* source; // all the items (a party's item list, PartyItemArray)
    int slots;                  // 9
};

// The forwarding strip: the camp's item strips and the exchange's; an item click goes to the owner.
class ForwardingStrip : public UiItemStrip {
public:
    using UiItemStrip::UiItemStrip;
    // The end faces dimmed (sprite alpha 0.195) where it can scroll on, the background cells shifted by the current tab's
    // fractional position, the window's sprites, eight item cells through the owner, the stack counts (not 1) on surface 0
    // and, unless a member is 9, the owner's number of each item its accept virtual takes on surface 1.
    void Draw(SceneRenderer* pass) override;
    // The item under the screen point, or null; sets the cursor for the strip's action mode.
    Item* ItemAt(int x, int y, int* cursor) override;
    // Rebuilds the visible list from the source list for the current filter and tab (tab 5 of filter 0 shows everything).
    void RebuildList() override;
    // The strip's own click, then the item under the point to the owner.
    void LeftDown(int flags, int x, int y) override;
};

// The figure strips (the hero screen's two, the exchange window's).
class FigureStrip : public UiItemStrip {
public:
    using UiItemStrip::UiItemStrip;
    // The figure strips (the hero screen's two, the exchange window's): the name list, the texture "InventoryM01", Build, the
    // arrows' tips 0x9DD0 / 0x9DD1 unless `noArrowTips`, no tabs, the six tab buttons' sprites (scene objects 2..7) hidden.
    void SetupFigures(std::vector<std::string>* names, bool top, bool noArrowTips);
    // The end faces dimmed where it can scroll on, the background and cells placed by the current tab's fraction, the frame
    // (scene object 8) on selected when in view, the window's sprites, then each visible name's hero face through the owner.
    void Draw(SceneRenderer* pass) override;
    // No item: null, `topic` not written.
    Item* ItemAt(int x, int y, int* topic) override;
    // Count_ = the number of names.
    void RebuildList() override;
    // The strip's own click, then on the items (region 8) the figure under the point picked: the "on_off" sound and the
    // owner's choose-figure virtual with its name.
    void LeftDown(int buttons, int x, int y) override;

    std::vector<std::string>* figures = nullptr;    // the figure names
};

// --- Camp screen ------------------------------------------------------------------------------

// A party hero's statistics as the camp reads them: the skill in a spell school rounded to nearest, and the
// whole "mana units" (the mana maximum stored as float, divided by the period, truncated).
int HeroSchoolSkill(UnitStats* stats, int school);
int HeroManaUnits(UnitStats* stats);
// Whether a hero with `stats` can use the spell `spell` (required skill, mana cost).
bool HeroCanUseSpell(UnitStats* stats, CSpell* spell);

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

    // Built inline by the main menu's Multiplayer click.
    explicit MpHeroScreen(Interface3D* ui) : ItemScreen(ui) {}
    // The heroes saved, the player's records handed to the client world, the preview cleared and the description panel emptied.
    void CloseScreen() override;
    // With a preview figure: the hero's statistics restored, its size factors, scale and unit statistics; then the statistics
    // panel redrawn.
    void RefreshStats() override;
    // The local player's first hero.
    PartyUnitRecord* HeroRecord() override;
    // The weight carried by the hero being made (weapon, armour, spell container and the quick items).
    float Weight() override;
    // Keeps the selected hero in the list and visible (ten rows), then shows it.
    void ClampHeroSelection();
    // The same for the voice list (eleven rows), then shows the voice's name.
    void ClampVoiceSelection();
    // What an item strip calls with its action mode: with mode 0, equips a weapon, armour or spell container; then refreshes.
    void TakeItem(Item* item, int mode) override;
    // Whether the hero can take `item`; spell containers need the spell's skill and mana.
    bool CanEquip(Item* item, int mode) override;

    void LeftUp(int flags, int x, int y) override;
    // Nothing while a hero is being renamed; after the base handler, clicks by mode (9 list, 10 new hero, 11 statistics):
    // see docs/notes/ui.md, "MpHeroScreen::LeftDown".
    void LeftDown(int flags, int x, int y) override;
    // After the container's handler, unless a child used the key: Enter, Esc and the paging keys by mode (9, 10, 11):
    // see docs/notes/ui.md, "MpHeroScreen::KeyDown". A key used is marked.
    void KeyDown(int key, int repeat, int flags) override;
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
    void ChooseFigure(std::string_view prototype) override;
    // The voices' surface (2): the "lmp_voices" title and eleven rows from voiceTop, the selection frame on the chosen one.
    void DrawVoices();
    // The hero list's surface (0): ten rows of names from heroTop, the selection frame on the selected hero.
    void DrawHeroList();

    // On the statistics page the experience lines on surface 2; then what is under the mouse for the description panel, the
    // children drawn and the hero previewed (list at depth 553, new hero at 400).
    void Draw(SceneRenderer* pass) override;
    // The children's update; a held rotate arrow turns the preview by twice the frame time; the scroll bars' moves redraw the
    // list and voices; on the new hero page the create button is bright only when the three attributes add up to the
    // attribute points and the name is not empty.
    void Update() override;
    void DrawExperience();
    // Region 6 of the list: the rename edit window enabled and editing; its text is the selected hero's name after its "|",
    // surface 3 shows the name before it plus " | ". Nothing without a selection.
    void Rename();
    // The hero's voice: the sound playing stopped, then one of the voice's sound-set entries with flags 0x2282 and a name, from
    // a random one on, played (voiceSound).
    void PlayVoice();
    // Enter / region 1 of the list: with a hero selected the heroes are saved and the connection screen opened; otherwise a
    // message box.
    void Continue();
    // Region 1 / Enter of the new hero: when the three attributes add up to the attribute points and a name is typed, the
    // first weapon, one more entry and the spell entry shown, the name given to the hero and the statistics panel, mode 11;
    // otherwise the cancel sound.
    void CreateHero();
    // The selected hero deleted: its file removed, later records moved down, the selection kept, the list's scroll range and
    // list redrawn. Nothing without a selection.
    void DeleteHero();
    // A message box answered: 0x10001 (the "lmp_deletepers" OK) deletes the hero; then the selected hero's record takes the
    // local player's party and sends the transfer; mode 9.
    void MessageResult(int result) override;
    // Region 1 / Enter of the statistics page: the new hero is kept and saved as a record: see docs/notes/ui.md,
    // "MpHeroScreen::AddNewHero".
    void AddNewHero();
    // The screen laid out for `mode` (9 list, 10 new hero, 11 statistics): see docs/notes/ui.md, "MpHeroScreen::SetMode".
    // Rebirth's WideScreen shows the curtains (sprites 19 and 20) in modes 10 and 11 and hides them in 9.
    void SetMode(int mode);
    // The screen's Create (reproduces Rebirth's WideScreen: the two curtains over the margins, sprites 19 and 20): see
    // docs/notes/ui.md, "MpHeroScreen::CreateScreen".
    void CreateScreen() override;
    // The hero list's scroll bar after the count changed (DeleteHero and AddNewHero inline it):
    // range count - 10 keeping the position, or 0 with the top row 0.
    void RefreshHeroScroll();

    int mode;                   // 9, 10, 11
    int heroIndex;              // selected hero
    UiScrollbar* heroScroll;   // scroll bar of the hero list
    int heroTop;                // first row shown
    CUnit* previewModel;        // the hero's figure shown by the preview
    Item* weapon;
    Item* quickItem;
    Item* spellContainer;
    HeroStatsPanel* statsPanel;
    HeroEquipPanel* weaponPanel;   // shows the weapon and the quick item
    HeroQuickStrip* spellPanel;
    FigureStrip* figureStrips[2]; // the figures of defaults (voices voiceLists[0]) and of names (voiceLists[1])
    TradePanel* tradePanel;
    std::vector<std::string> defaults;    // the men's figures; its first entry goes to ChooseFigure for a new hero
    std::vector<std::string> names;       // the women's figures
    float turn;                 // the preview's turn
    int rotating;               // 0, 1 or 2 while a rotate arrow is held
    UiEditField* renameEdit;   // the hero list's rename edit window (focused while renaming)
    UiEditField* nameEdit;     // the new hero's name edit window
    UiScrollbar* voiceScroll;
    std::string text;
    std::vector<MpVoiceEntry> voiceLists[2];    // voices points at one of them
    std::vector<MpVoiceEntry>* voices;      // voiceLists[0] or [1]
    int voiceTop;
    int voiceIndex;
    int voiceSound;
};

// --- Multiplayer connection screen --------------------------------------------------------------
// The servers found (config\addrbook.ini keeps the ones asked directly): a list of 15 rows with a scroll bar, row 0
// the entry that hosts a new game. Regions: 0 connect / host, 1 back, 2 ask the master server, 3 add / forget an
// address, 4 the unlisted switch, 5 the password, 6-9 the sort columns, 10-13 the game type tabs, 14 and up the list
// rows. Sprites: 4 the unlisted switch, 5 the password's, 6-9 the game type tabs (textureOffsetU 0.09 selected).

// An entry of the connection screen's list.
struct MpAddressEntry {
    std::string name;             // the server's name
    std::string description;      // the current map's title
    int players;
    int maxPlayers;
    int ping;                      // milliseconds (9999: none)
    net::NetAddress address;
    int gameType;                  // 0..3 (4: the new game entry)
    bool foreign;                  // the server answered with another protocol
    int state;                     // net::ServerEntryBase::State (1: asked directly, forgettable)
    bool password;                 // joining needs a password
};

// The colour the connect list's texts and its line are drawn in: set per row and for the details by the list draw
// and left as it is for the next draw's titles; Create's texts take it too.
inline std::uint32_t g_connectTextColour = /*00793AD8*/ 0xA7D7E4;

class MpConnectScreen : public UiContainer {
public:
    using UiContainer::UiContainer;
    void CloseScreen() override {}

    // The message box results: the kinds the screen opens boxes with, ORed with 1 (OK) or 2 (cancel) when
    // they come back.
    enum BoxKind : std::uint32_t {
        kBoxNotice = 0x10000,         // a message (refusal, wrong protocol)
        kBoxAddress = 0x20000,        // an address to ask
        kBoxPassword = 0x30000,       // the new game's password
        kBoxMaster = 0x40000,         // the master server request (MasterRequestBox)
        kBoxJoinPassword = 0x50000,   // the password of the server joined
    };
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
    void MessageResult(int result) override;
    // By region (above): 0 Connect, 1 back (the cancel sound, the address book written, the network sessions dropped, the
    // screen closed with 0x1000001), 2 MasterRequest, 3 EditAddress, 4 the unlisted switch, 5 the password (none: a box for
    // one; else it is dropped), 6-9 the sort column (a second click reverses it), 10-13 the game type (only with row 0
    // selected), 14 and up a row (the select sound, it and its address selected); then the container's handler.
    void LeftDown(int buttons, int x, int y) override;
    // A row double-clicked is selected (no sound, no drawing) and Connect follows; then the container's handler.
    void LeftDouble(int buttons, int x, int y) override;
    // The container's handler first; then Enter connects, Esc leaves (as region 1 without the address book), Up / Down / PgUp /
    // PgDn move the selection by 1 / 15 with the select sound (SelectAndScroll); those keys are marked used.
    void KeyDown(int key, int repeat, int flags) override;
    // The selection clamped to the list (an empty list gives -1 and reads the entry before it), its address kept, the list
    // scrolled to show it and drawn.
    void SelectAndScroll();
    // The OK sound; row 0 hosts (GameFlow::StartServer: the name typed or the default, the player slider + 1,
    // gameType, hostPassword, unlisted, the client's queries' extra word) after the address book is written;
    // another row joins: a foreign server gets the "lmp_wrong_protocol" box; otherwise the address book is
    // written, a password server opens the password box, the server is joined at once with no password and the
    // client's game type set from the row.
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
    // The list drawn on surface 0 (Rebirth-patched: WideScreen and ExtendedServerName reproduced behind their flags): see
    // docs/notes/ui.md, "MpConnectScreen::DrawListVanilla".
    void DrawListVanilla();
    // The list again: the client's servers (ServerAt) in order, sorted by sortKey (std::sort), the new game entry
    // inserted first; the selection follows its address (row 0 and address 0 when gone); the scroll bar's range is
    // the rows past 15 and its position set again; scrollTop 0.
    void RebuildList();

    float refreshTimer;            // seconds since the last refresh (0.5 s period)
    std::vector<MpAddressEntry> entries;
    int selected;                  // row
    net::NetAddress selectedAddress{};    // the selected row's address (0: row 0)
    int scrollTop;                 // the first row shown
    UiScrollbar* scroll;
    UiSlider* playersSlider;      // the new game's maximum players - 1
    UiEditField* nameEdit;        // the new game's name
    int maxPlayers;                // the slider's position + 1 (6 from Create)
    int gameType;                  // the new game's type tab 0..3
    char boxText[0x80];            // the edit boxes' text (an address, a password)
    bool unlisted;                 // the new game is not announced (1 from Create)
    std::string hostPassword;     // the new game's password
    std::vector<std::string> allodNames; // "lmp_allod_name %d" texts (the game type tabs)
    bool joining;                  // the client was asking a host last frame
    std::vector<std::string> texts;       // "lmp_connection %d" texts (22)
    int sortKey;                   // 0 none; 1..8 the column pairs (odd ascending)
    bool newGameTips;              // the unlisted and password regions have their tips (DrawList; 0 from Create)
};

// A message box waiting on a master server request: one cancel button, the link to the master server and a progress bar
// sprite. Its result is kBoxMaster (| 2 when done).
class MasterRequestBox : public MessageWindow {
public:
    // The box (flags 2: one cancel button; format 1), the link connecting to `address`, the
    // bar (a plain sprite, colour 0xFFA06800), and `extra` written to the link's outgoing buffer.
    MasterRequestBox(Interface3D* ui, std::string_view titleKey, const char* textKey, std::uint32_t result,
                      const net::NetAddress* address, std::uint32_t extra);
    // The bar's right edge at 225 + 355 * the received share; the link steps: done (3) hands the reply to the
    // client's server list; the box closes (0x40002) when the link is done or, stepping again, closed (up to
    // three steps a frame).
    void Update() override;

    net::MasterLink link;
    UiMesh* bar;
};

// --- In-game escape menu ----------------------------------------------------------------------
// Opened by Esc during a game. Regions, built from the EscMenu layout: 0 save, 1 load, 2 resume, 3 quit
// (message box), 4 options.
class EscMenu : public UiContainer {
public:
    explicit EscMenu(Interface3D* ui) : UiContainer(ui) {}
    void MouseMove(int flags, int x, int y) override;
    void KeyDown(int key, int repeat, int flags) override;
    void MessageResult(int result) override;
    // The stone model and the five regions, whose rectangles come from menus.reg.
    void CreateScreen() override;
    // Opens the save window (region 0), the load window (1; neither in multiplayer), resumes (2), asks to quit (3) or opens the
    // options (4).
    void LeftDown(int buttons, int x, int y) override;
    // The menu closes: its stone model (models[0]) plays animation 0.
    void CloseScreen() override;

    int hovered;                // region under the mouse, -1 for none
};

// --- Credits ------------------------------------------------------------------------------------
// The titles window (main menu region 4). A line of the titles' script:
struct TitleLine {
    bool scroll;                // the first word is "#scroll"
    float speed;                // the second word (atof): the scroll speed, or the seconds shown
    std::string text;          // the fourth word (the header)
    std::string style;         // the third word (the text file in the archive)
};

// The titles' scrolling text surface.
class UiScrollingSurface : public UiSurface {
public:
    // A surface of format 5 over the layout rectangle (keyed on white); no text, the offsets and the speed 0
    // (startTime is left as it was).
    UiScrollingSurface(Interface3D* ui, const RECT* rect);
    virtual ~UiScrollingSurface();
    // Loads the text file `name` of `archive` into the surface (LoadRtf); whether it was found.
    bool Load(MappedResource* archive, const char* name);
    // Reads the RTF block and lays it out at the surface's width (render_rich_text.h), the offset 0, the text
    // rendered.
    void LoadRtf(const void* data, std::uint32_t size);
    // While the speed is not 0 the offset moves by the game time passed times the speed (truncated) and the
    // text is rendered again; then the surface's own draw.
    void Blit() override;
    // Starts the text at `speed` (0: still), from below the surface when `fromBelow` (the offset minus the surface's height in
    // twips); the end offset 0, the start time the game time.
    void Start(float speed, bool fromBelow);
    // Draws the text onto the canvas, white first, its top `offset` twips above the surface's. The first time the whole
    // text fits above 1000 twips below the surface the offset is kept; once the offset has moved 1000 twips plus the
    // height past it, the speed and the kept offset become 0 (RichEdit's EM_FORMATRANGE end in the original).
    void RenderText();

    float startTime = 0.0f;
    int offsetX = 0;            // twips
    int offset = 0;             // twips scrolled
    float speed = 0.0f;         // 0 once the text has scrolled through
    int endOffset = 0;          // offset when the end of the text came into view (0: not yet)
    std::unique_ptr<render::text::RichLayout> text;    // the loaded text laid out (null before LoadRtf)
};

class CreditsScreen : public UiContainer {
public:
    CreditsScreen(Interface3D* ui, bool fromGame, const char* name);
    void LeftDown(int flags, int x, int y) override;
    void KeyDown(int key, int repeat, int flags) override;
    // Ends the titles: back to where they were opened from.
    void Close();
    // Opened in a game (fromGame 0) the loading screen steps and the music stops; the cursor hidden, the
    // "<name>fin" movies played, "outro.res" opened and its "credits.scr" parsed
    // into lines (one record per line: "#scroll" flag, speed, two texts); the scrolling text surface and the
    // header surface, both frozen; the display's flag kept in savedDisplay and cleared; the player starts
    // "Movies\<name>.bik".
    void CreateScreen() override;
    // Once the game time passes nextLineAt and the text has stopped: both surfaces frozen, then the next
    // line: its text file loaded and started (a "#scroll" line scrolls at its speed under its header, another
    // stays for its speed in seconds), or past the last line finished. The movie steps (stretched unless the
    // movie flag is 1); when it has ended after the last line: "<name>fout" played, the screen flag cleared
    // and the titles closed as Close() does; otherwise the container's draw.
    void Draw(SceneRenderer* pass) override;
    // The lines freed, the display's flag restored, the screen flag set and the cursor shown.
    void CloseScreen() override;
    // A window above closed: the movie player is closed.
    void MessageResult(int result) override;

    UiScrollingSurface* textSurface = nullptr;    // the scrolling text
    UiSurface* headerSurface = nullptr;
    MappedResource archive;    // "outro.res"
    std::vector<TitleLine> lines;
    float nextLineAt = 0.0f;           // game time the current line ends (0 from Build and for scrolls)
    std::size_t nextLine = 0;          // the index of the next line (lines.size() at the end)
    std::string name;          // ("Titles")
    BinkVideo player;          // the movie player
    bool fromGame;              // opened in a game (pending value 0xCCCCCCCC) or the menu
    bool finished = false;              // the last line has gone (cleared by Build)
    int savedDisplay = 0;           // the renderer's fog colour while the titles run
};

// --- Main menu ----------------------------------------------------------------------------------
// The main menu (CreateScreen id 2). Regions 0-5 from the menus.reg "MainMenu" rectangles; the stones are the object
// manager's object 2; the world behind it runs the "MainMenu.cam" camera path with rain at night.
class MainMenu : public UiContainer {
public:
    explicit MainMenu(Interface3D* ui) : UiContainer(ui) {}
    void Draw(SceneRenderer* pass) override;
    // The map edge fade put back, the sounds and the ambience stopped.
    void CloseScreen() override;
    // By region: 0 the difficulty question (a message box), 1 load (the save/load window), 2 the exit movie and quit, 3
    // options, 4 the titles (surfaces cleared, opened over the menu), 5 multiplayer (status "reserved_04", the server flag, the
    // hero screen). Then the container's handler.
    void LeftDown(int buttons, int x, int y) override;
    // The interface's saved state cleared, the flow's party step 0, an inert extra input side (UiInertInput),
    // the full-screen rectangle, the version line, the six regions; the default music, the map edge fade kept
    // and set to 6, rain by a coin toss, daylight between 7 and 21, the rain started before 7; the camera
    // path "MainMenu.cam" and the menu music.
    void CreateScreen() override;
    // A surface (100, 480)-(780, 600) with the "version_name" text, added to the window.
    void AddVersionLine();
    // The children's update; with rain the weather turns on at 0 o'clock and off at 7 (the truncated hour
    // changing); at night before 6 with rain and an option bit, every 100 ms a 1 in 41 chance of a lightning
    // bolt at random; the hour kept.
    void Update() override;
    // A message box's answer (the flow's party step 0 and the server flag cleared first): 0x10001 (the quit
    // box's yes) the exit movie and quit, 0x20001 the intro movie and the flow's state 3, 0xCCCCCCCC (the
    // titles closed) the version line again.
    void MessageResult(int result) override;
    // Hovering a region plays its stone's animation (regions 0-5: 1, 3, 5, 7, 11, 9; off them the rest pose) with the stone
    // sound, once per region; then the container's handler.
    void MouseMove(int buttons, int x, int y) override;
    // The container's handler, then Esc asks to quit (a message box "quit" / "quit_msg", flags 0x10000).
    void KeyDown(int key, int repeat, int flags) override;

    int hovered = -1;                     // the region under the mouse (-1 none)
    bool rain = false;
    bool daylight = false;                   // the hour was between 7 and 21 when the menu opened
    float hour = 0.0f;                      // the time of day at the last update
    float savedEdgeFade = 0.0f;             // the map edge fade before the menu
};

// Trading between two players (screen 9, Xch_* texts). Multiplayer only.
class TradeGrid;
class ExchangeScreen : public ItemScreen {
public:
    explicit ExchangeScreen(Interface3D* ui);
    // The description panel describes nothing and the scratch party is deleted.
    void CloseScreen() override;
    // The exchange went through: the local party's items are released, every item of `partyC`, then of
    // `partyB`, returned into it (ReturnAll), its money set to partyB's - partyA's + partyC's, and the client
    // goes back to the world.
    void ExchangeDone(CParty* partyB, CParty* partyA, CParty* partyC) override;
    // Mode 1 (the strip): the put_on sound, `item` moves from the local party into our offer (MoveItem with
    // limit 8, merging) and the offer is published. Mode 5 (the grid), only for an item of our offer: the
    // put_off sound, the item moves back to the local party, the strip scrolls to what the party now holds and
    // the offer is published. Other modes nothing.
    void TakeItem(Item* item, int mode) override;
    // Every item may move.
    bool CanEquip(Item*, int) override { return true; }
    // Builds the window (texture "campinfo", regions 0 accept, 1 take back, 2 leave): see docs/notes/ui.md,
    // "ExchangeScreen::CreateScreen".
    void CreateScreen() override;
    // The item (or help topic) under the mouse from the strip, else the grid, goes to the description panel;
    // the four surfaces get "xch_current_money" with the local party's money and "xch_accept",
    // "xch_money_offer", and while the partner's offer pairs with ours "xch_money_remote" with its money and
    // "xch_remote_header" with "xch_remote_ok" / "_cancel"; the accept / take back buttons are shaded by our
    // offer's acceptance and the third button by the partner's; then the children.
    void Draw(SceneRenderer* pass) override;
    void KeyDown(int key, int repeat, int flags) override;
    // The children's update; then, when the client's offer is gone or detached, our offer's items go back to
    // the local party and the client returns to the world; else a changed money field (its changed cleared)
    // becomes our offer's money (atoi) and the offer is published.
    void Update() override;
    // The container's handler, then by region: 0 accept (the buy sound and the trade recorded with the local
    // party, when our offer is live and not accepted and its money is within the local party's; else the
    // cancel sound), 1 take the acceptance back (cancel sound and the recorded take-back, when it is
    // accepted), 2 leave (the transit sound and the recorded leave). The event is not marked used.
    void LeftDown(int buttons, int x, int y) override;

    CParty* scratch;                 // a party of the screen's own (made by Create, deleted by Close)
    CParty* partnerOffer;            // the client's parties[1]
    CParty* ownOffer;                // the client's parties[0]
    UiItemStrip* strip;             // the local party's items (forwarding strip, mode 1)
    FigureStrip* figures;           // the players (figure strip)
    TradeGrid* grid;                // the two offers (mode 5)
    UiEditField* moneyField;        // the money offered
    std::vector<std::string> names;            // the figure strip's names
};
