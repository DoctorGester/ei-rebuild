#pragma once

// Interface screens (docs/notes/ui.md, "Screens"): the camp's item strips, the multiplayer hero and
// connection screens, the escape menu, the credits, the main menu and the exchange window. Handler arguments:
// mouse (button flags, x, y) in screen pixels; key down (virtual-key code, repeat count, scan code and
// flags); wheel (key flags, wheel delta, x, y).

#include "bink_video.h"
#include "ui.h"
#include "ui_item_info.h"
#include "world.h"
#include "net_classes.h"

#include "game_data.h"
#include "resource.h"
#include "lib/static_object.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

class CHeroEquipPanel;
class CHeroQuickStrip;
class CHeroStatsPanel;
class CTradePanel;

namespace render::text {
struct RichLayout;
} // namespace render::text

// The input sub-object that captured the mouse (scroll arrows held down), or null.
inline void*& UiCapture(CInterface3D* ui) { return ui->mouseCapture_; }
// A shift key is held (set by the key-down dispatch).
inline bool UiShiftDown(const CInterface3D* ui) { return ui->shiftDown_ != 0; }

namespace uiorig {
void PlayUiSound(CInterface3D* ui, const char* name, int flags);
// Leaves the running game for the main menu (the game object global).
void LeaveGame();
} // namespace uiorig

// The item predicates of the camp screens. Loot items (type ItemKind::Loot, CItemLoot) are sorted by kind and record
// type.
bool IsPart(const CItem* item);             // kind 2, record type 0-2 (blueprint parts)
bool IsMaterial(const CItem* item);
bool IsSpell(const CItem* item);            // kind 2, record type 3 (the spell table)
bool IsModifier(const CItem* item);         // kind 2, record type 4 (spell modifier table)
bool IsSpellContainer(const CItem* item);   // type ItemKind::Spell
// An item the item constructor can take apart: a weapon or armour, or a quick item whose prototype's material
// class is not "none"; never when the prototype exists and is not buildable.
bool IsBuildable(const CItem* item);

// --- Camp item strip ------------------------------------------------------------------------
// The horizontal strip of items (or spells) at the bottom (y 500) or top (y 0) of the camp screens: six tab regions
// choose a category, two arrows scroll it, the strip itself shows `visible_` slots. Created by the camp screen and
// the exchange window. Regions: 0/1 arrows, 2-7 tabs, 8 the strip.

// An item panel (CUiItemPanel): mode_ is what clicking an item does (1 equip, 2, 3, cursor 0x16-0x18); owner_ is the
// screen that owns the strip (item clicks, predicates).
class CUiItemStrip : public CUiItemPanel {
public:
    // The plain strip (the visible-list virtual is pure), the base of the forwarding and the figure strips.
    using CUiItemPanel::CUiItemPanel;
    virtual void RebuildList() = 0;
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnLButtonUp(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    int Wheel(int f, int d, int x, int y) override { return OnWheel(f, static_cast<short>(d), x, y); }

    // For the camp's three strips and the exchange window's: the background texture "Inventory02" (`top`) or "Inventory01", the
    // strip built, the arrows' tips 0x4FB0 / 0x4FB1, two text surfaces (x 50..750, y top_ + 15..35 and top_ + 65..85), nine
    // slots and the source list.
    void Setup(std::vector<CItem*>* source, char top);
    // The figure strips (the hero screen's two, the exchange window's): the name list, the texture "InventoryM01", Build, the
    // arrows' tips 0x9DD0 / 0x9DD1 unless `noArrowTips`, no tabs, the six tab buttons' sprites (scene objects 2..7) hidden.
    void SetupFigures(std::vector<std::string>* names, char top, char noArrowTips);
    void SetSlots(int slots);
    // Lays the strip out along the top edge (`top`: y 0, scale 8) or the bottom (y 500, scale 4): the scroll state cleared, nine
    // regions (arrows, six tab buttons, the items), the ends' and tab buttons' sprites, the highlight frame, the background
    // cells; tab 5.
    void Build(char top);
    // Selects tab `tab`: settles the current tab to a whole slot, highlights its button, stops scrolling.
    void SetTab(int tab);
    // Chooses what the tabs sort by (0 items, 1 spells, 2 armour), loads their tooltips and resets every tab's position.
    void SetFilter(int filter);
    // Tab selection used by the camp screen while tabs are enabled: select `tab`, or cycle within [first, first + count).
    void SelectTab(int tab);
    void CycleTab(int first, int count);
    // The tab (0-5) an item belongs to under each filter.
    int ItemTab(const CItem* item);
    int SpellTab(const CItem* item);
    int ArmorTab(const CItem* item);
    // Rebuilds the list as RebuildList does, first switching to `item`'s tab (SetTab, while the tab keys are enabled; not for
    // filter 0's tab 5), and scrolls the tab so the last entry the same as `item` is visible: its position moves into
    // [index - 6, index]. The scroll stops; count_ is not updated. Null does nothing.
    void ScrollToItem(CItem* item);
    // The tab of `item` under the current filter (0 for an unknown filter), as both rebuilds choose it.
    int TabOf(const CItem* item);
    // Every frame. Scrolls: a held arrow adds one slot to the pending scroll at once and one more every 0.1 s; the pending
    // scroll moves the current tab's position at an accelerating speed (15 slots/s^2, at most 7), easing out within half a
    // slot and settling on a whole slot; then the list is rebuilt and the position kept within [0, count - 7].
    void Update() override;

    void OnLButtonDown(int flags, int x, int y);
    void OnLButtonUp(int flags, int x, int y);
    void OnMouseMove(int flags, int x, int y);
    BOOL OnWheel(int keys, short delta, int x, int y);

    // Settles the pending scroll on release.
    void SettleScroll();

    int tab_;                   // 0-5
    int scrollDirection_;       // 0 none, 1 left arrow held, 2 right arrow held
    float scrollTimer_;
    float position_[6];         // scroll position of each tab, in slots
    float pending_;             // scroll still to apply, in slots
    float velocity_;
    int selected_;              // -1; the hero screen's figure strips: the chosen figure
    int top_;                   // y of the strip: 500 (bottom) or 0 (top)
    float scale_;
    int count_;                 // visible list size after the last rebuild
    bool tabsEnabled_;
    int hovered_;               // -1
    union {
        int filter_;                // 0 items, 1 spells, 2 armour
        std::vector<std::string>* figures_;   // the figure strips: the figure names
    };
    std::vector<CItem*> list_;          // the visible items
    std::vector<CItem*>* source_; // all the items (a party's item list, PartyItemArray)
    int slots_;                 // 9
};

// The forwarding strip: the camp's item strips and the exchange's; an item click goes to the owner.
class CForwardingStrip : public CUiItemStrip {
public:
    CForwardingStrip(CInterface3D* ui, CItemScreen* owner);
    ~CForwardingStrip() override;
    // The end faces dimmed (sprite alpha 0.195) where it can scroll on, the background cells shifted by the current tab's
    // fractional position, the window's sprites, eight item cells through the owner, the stack counts (not 1) on surface 0
    // and, unless a member is 9, the owner's number of each item its accept virtual takes on surface 1. Returns 1.
    BOOL Draw(CSceneRenderer* pass) override;
    // The item under the screen point, or null; sets the cursor for the strip's action mode.
    CItem* ItemAt(int x, int y, int* cursor) override;
    // Rebuilds the visible list from the source list for the current filter and tab (tab 5 of filter 0 shows everything).
    void RebuildList() override;
    int LeftDown(int b, int x, int y) override { return OnLButtonDownForward(b, x, y), 0; }
    // The strip's own click, then the item under the point to the owner.
    void OnLButtonDownForward(int flags, int x, int y);
};

// The figure strips (the hero screen's two, the exchange window's).
class CFigureStrip : public CUiItemStrip {
public:
    using CUiItemStrip::CUiItemStrip;
    // The end faces dimmed where it can scroll on, the background and cells placed by the current tab's fraction, the frame
    // (scene object 8) on selected_ when in view, the window's sprites, then each visible name's hero face through the owner.
    BOOL Draw(CSceneRenderer* pass) override;
    // No item: null, `topic` not written.
    CItem* ItemAt(int x, int y, int* topic) override;
    // Count_ = the number of names.
    void RebuildList() override;
    int LeftDown(int b, int x, int y) override { return OnFigureClick(b, x, y), 0; }
    // The strip's own click, then on the items (region 8) the figure under the point picked: the "on_off" sound and the
    // owner's choose-figure virtual with its name.
    void OnFigureClick(int buttons, int x, int y);
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
class CMpHeroScreen : public CItemScreen {
public:
    explicit CMpHeroScreen(CInterface3D* ui);
    ~CMpHeroScreen() override;
    void CloseScreen() override { Close(); }
    void MessageResult(int result) override { OnMessageResult(result); }
    void TakeItem(CItem* item, int mode) override { OnItemChosen(item, mode); }
    void RefreshStats() override { RefreshHero(); }
    PartyUnitRecord* HeroRecord() override { return GetHero(); }
    double Weight() override { return EquipmentWeight(); }
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnLButtonUp(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    // Keeps the selected hero in the list and visible (ten rows), then shows it.
    void ClampHeroSelection();
    // The same for the voice list (eleven rows), then shows the voice's name.
    void ClampVoiceSelection();
    // The weight carried by the hero being made (weapon, armour, spell container and the quick items), unrounded.
    double EquipmentWeight();
    // What an item strip calls with its action mode: with mode 0, equips a weapon, armour or spell container; then refreshes.
    void OnItemChosen(CItem* item, int mode);
    // The local player's first hero.
    PartyUnitRecord* GetHero();
    // Whether the hero can take `item`; spell containers need the spell's skill and mana.
    bool CanEquip(CItem* item, int mode) override;

    void OnLButtonUp(int flags, int x, int y);
    // Nothing while a hero is being renamed; after the base handler, clicks by mode (9 list, 10 new hero, 11 statistics):
    // see docs/notes/ui.md, "CMpHeroScreen::OnLButtonDown".
    void OnLButtonDown(int flags, int x, int y);
    // After the container's handler, unless a child used the key: Enter, Esc and the paging keys by mode (9, 10, 11):
    // see docs/notes/ui.md, "CMpHeroScreen::OnKeyDown". A key used is marked.
    void OnKeyDown(int key, int repeat, int flags);
    // Enter while renaming: the hero's name becomes the text before its " | " plus " | " and the typed text (trimmed); the
    // transfer sent; the edit window put away.
    void FinishRename();
    // The rename edit window put away (its surface hidden, disabled, editing off).
    void CloseRenameEdit();
    // Back from the statistics page to the new hero (mode 10).
    void BackToNewHero();

    // Shows hero `index` (none when not in the list): copied into the local party and made current, the preview rebuilt, the
    // unit re-armed, the turn reset and the list redrawn.
    void ShowHero(int index);
    // The preview figure of the local player's first record: see docs/notes/ui.md, "CMpHeroScreen::MakePreview".
    void MakePreview(int flag);
    // The new hero takes figure (unit prototype) `prototype`: see docs/notes/ui.md, "CMpHeroScreen::ChooseFigure".
    void ChooseFigure(std::string_view prototype) override;
    // The voices' surface (2): the "lmp_voices" title and eleven rows from voiceTop_, the selection frame on the chosen one.
    void DrawVoices();
    // The hero list's surface (0): ten rows of names from heroTop_, the selection frame on the selected hero.
    void DrawHeroList();
    void CallShowHero(int index);

    // On the statistics page the experience lines on surface 2; then what is under the mouse for the description panel, the
    // children drawn and the hero previewed (list at depth 553, new hero at 400). Returns 1.
    BOOL Draw(CSceneRenderer* pass) override;
    // The children's update; a held rotate arrow turns the preview by twice the frame time; the scroll bars' moves redraw the
    // list and voices; on the new hero page the create button is bright only when the three attributes add up to the
    // attribute points and the name is not empty.
    void Update() override;
    // The heroes saved, the player's records handed to the client world, the preview cleared and the description panel emptied.
    void Close();
    void DrawExperience();
    // Region 6 of the list: the rename edit window enabled and editing; its text is the selected hero's name after its "|",
    // surface 3 shows the name before it plus " | ". Nothing without a selection.
    void Rename();
    // The hero's voice: the sound playing stopped, then one of the voice's sound-set entries with flags 0x2282 and a name, from
    // a random one on, played (voiceSound_).
    void PlayVoice();
    // Enter / region 1 of the list: with a hero selected the heroes are saved and the connection screen opened; otherwise a
    // message box.
    void Continue();
    // With a preview figure: the hero's statistics restored, its size factors, scale and unit statistics; then the statistics
    // panel redrawn.
    void RefreshHero();
    // Region 1 / Enter of the new hero: when the three attributes add up to the attribute points and a name is typed, the
    // first weapon, one more entry and the spell entry shown, the name given to the hero and the statistics panel, mode 11;
    // otherwise the cancel sound.
    void CreateHero();
    // The selected hero deleted: its file removed, later records moved down, the selection kept, the list's scroll range and
    // list redrawn. Nothing without a selection.
    void DeleteHero();
    // A message box answered: 0x10001 (the "lmp_deletepers" OK) deletes the hero; then the selected hero's record takes the
    // local player's party and sends the transfer; mode 9.
    void OnMessageResult(int result);
    // Region 1 / Enter of the statistics page: the new hero is kept and saved as a record: see docs/notes/ui.md,
    // "CMpHeroScreen::AddNewHero".
    void AddNewHero();
    // The screen laid out for `mode` (9 list, 10 new hero, 11 statistics): see docs/notes/ui.md, "CMpHeroScreen::SetModeVanilla".
    // Rebirth's WideScreen shows the curtains (sprites 19 and 20) in modes 10 and 11 and hides them in 9.
    void SetModeVanilla(int mode);
    // The screen's Create (reproduces Rebirth's WideScreen: the two curtains over the margins, sprites 19 and 20): see
    // docs/notes/ui.md, "CMpHeroScreen::CreateScreen".
    void CreateScreen() override;
    // The hero list's scroll bar after the count changed (DeleteHero and AddNewHero inline it):
    // range count - 10 keeping the position, or 0 with the top row 0.
    void RefreshHeroScroll();

    int mode_;                  // 9, 10, 11
    int heroIndex_;             // selected hero
    CUiScrollbar* heroScroll_;  // scroll bar of the hero list
    int heroTop_;               // first row shown
    CUnit* previewModel_;       // the hero's figure shown by the preview
    CItem* weapon_;
    CItem* armor_;
    CItem* spellContainer_;
    CHeroStatsPanel* statsPanel_;
    CHeroEquipPanel* weaponPanel_;  // shows the weapon and the armour
    CHeroQuickStrip* spellPanel_;
    CUiItemStrip* figureStrips_[2]; // the figures of defaults_ (voices voiceLists_[0]) and of names_ (voiceLists_[1])
    CTradePanel* tradePanel_;
    std::vector<std::string> defaults_;   // the men's figures; its first entry goes to ChooseFigure for a new hero
    std::vector<std::string> names_;      // the women's figures
    float turn_;                // the preview's turn
    int rotating_;              // 0, 1 or 2 while a rotate arrow is held
    CUiEditField* renameEdit_;  // the hero list's rename edit window (focused_ while renaming)
    CUiEditField* nameEdit_;    // the new hero's name edit window
    CUiScrollbar* voiceScroll_;
    std::string text_;
    std::vector<MpVoiceEntry> voiceLists_[2];   // voices_ points at one of them
    std::vector<MpVoiceEntry>* voices_;     // voiceLists_[0] or [1]
    int voiceTop_;
    int voiceIndex_;
    int voiceSound_;
};

// --- Multiplayer connection screen --------------------------------------------------------------
// The servers found (config\addrbook.ini keeps the ones asked directly): a list of 15 rows with a scroll bar, row 0
// the entry that hosts a new game. Regions: 0 connect / host, 1 back, 2 ask the master server, 3 add / forget an
// address, 4 the unlisted switch, 5 the password, 6-9 the sort columns, 10-13 the game type tabs, 14 and up the list
// rows. Sprites: 4 the unlisted switch, 5 the password's, 6-9 the game type tabs (textureOffsetU_ 0.09 selected).

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

class CMpConnectScreen : public CUiContainer {
public:
    explicit CMpConnectScreen(CInterface3D* ui) : CUiContainer(ui) {
        selectedAddress_ = net::NetAddress{};
    }
    ~CMpConnectScreen() override;
    void CloseScreen() override {}
    void MessageResult(int result) override { OnBoxResult(static_cast<std::uint32_t>(result)); }
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int LeftDouble(int b, int x, int y) override { return OnLButtonDouble(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }

    // The message box results: the kinds the screen opens boxes with, ORed with 1 (OK) or 2 (cancel) when
    // they come back.
    enum BoxKind : std::uint32_t {
        kBoxNotice = 0x10000,         // a message (refusal, wrong protocol)
        kBoxAddress = 0x20000,        // an address to ask
        kBoxPassword = 0x30000,       // the new game's password
        kBoxMaster = 0x40000,         // the master server request (CMasterRequestBox)
        kBoxJoinPassword = 0x50000,   // the password of the server joined
    };
    static constexpr int kRows = 15;

    // The screen over 800 x 600 (the vanilla body; Rebirth patches it): see docs/notes/ui.md,
    // "CMpConnectScreen::CreateScreen".
    void CreateScreen() override;
    // Surface 1 ("connecting") shown while the client asks a host; when it stops asking, a refusal box ("lmp_server_full",
    // "..._you_are_banned", "..._wrong_password", else "..._cannot_connect"; text key + "_msg") and nothing else this frame.
    // Otherwise the children, and every 0.5 s the client asks the selected address (none for row 0) and the list is rebuilt
    // and drawn; the scroll bar, the player slider (maxPlayers_ = its position + 1) and a changed name redraw the list.
    void Update() override;
    // A box answered: an address (0x20001) is parsed (port 8888 unless given) and asked when known, the
    // address book written, the list drawn; the master request ending (0x40002) draws the list; the new
    // game's password (0x30001 / 0x30002) is kept and its switch shows whether there is one; a join password
    // (0x50001) joins the selected server with it and sets the client's game type from the row.
    void OnBoxResult(std::uint32_t result);
    // By region (above): 0 Connect, 1 back (the cancel sound, the address book written, the network sessions dropped, the
    // screen closed with 0x1000001), 2 MasterRequest, 3 EditAddress, 4 the unlisted switch, 5 the password (none: a box for
    // one; else it is dropped), 6-9 the sort column (a second click reverses it), 10-13 the game type (only with row 0
    // selected), 14 and up a row (the select sound, it and its address selected); then the container's handler.
    void OnLButtonDown(int buttons, int x, int y);
    // A row double-clicked is selected (no sound, no drawing) and Connect follows; then the container's handler.
    void OnLButtonDouble(int buttons, int x, int y);
    // The container's handler first; then Enter connects, Esc leaves (as region 1 without the address book), Up / Down / PgUp /
    // PgDn move the selection by 1 / 15 with the select sound (SelectAndScroll); those keys are marked used.
    void OnKeyDown(int key, int repeat, int flags);
    // The selection clamped to the list (an empty list gives -1 and reads the entry before it), its address kept, the list
    // scrolled to show it and drawn.
    void SelectAndScroll();
    // The OK sound; row 0 hosts (GameFlow::StartServer: the name typed or the default, the player slider + 1,
    // gameType_, hostPassword_, unlisted_, the client's queries' extra word) after the address book is written;
    // another row joins: a foreign server gets the "lmp_wrong_protocol" box; otherwise the address book is
    // written, a password server opens the password box, the server is joined at once with no password and the
    // client's game type set from the row.
    void Connect();
    // On a valid selection: an entry asked directly is forgotten (and the address book written); another
    // opens the address box (texts_[19]) with the row's address (empty for row 0).
    void EditAddress();
    // The master server request: the master address (a text in the game's data, port 28004) in a
    // CMasterRequestBox (texts_[20]) with the client's queries' extra word (EI_Plugin's NewMaster with a
    // second master set: the master tried first).
    void MasterRequest();
    // Writes the client's queries to config\addrbook.ini (a chunk archive, chunk 1).
    void SaveAddressBook();
    // The list drawn on surface 0 (Rebirth-patched: WideScreen and ExtendedServerName reproduced behind their flags): see
    // docs/notes/ui.md, "CMpConnectScreen::DrawListVanilla".
    void DrawListVanilla();
    // The list again: the client's servers (ServerAt) in order, sorted by sortKey_ (std::sort), the new game entry
    // inserted first; the selection follows its address (row 0 and address 0 when gone); the scroll bar's range is
    // the rows past 15 and its position set again; scrollTop_ 0.
    void RebuildList();

    float refreshTimer_;           // seconds since the last refresh (0.5 s period)
    std::vector<MpAddressEntry> entries_;
    int selected_;                 // row
    net::NetAddress selectedAddress_;   // the selected row's address (0: row 0)
    int scrollTop_;                // the first row shown
    CUiScrollbar* scroll_;
    CUiSlider* playersSlider_;     // the new game's maximum players - 1
    CUiEditField* nameEdit_;       // the new game's name
    int maxPlayers_;               // the slider's position + 1 (6 from Create)
    int gameType_;                 // the new game's type tab 0..3
    char boxText_[0x80];           // the edit boxes' text (an address, a password)
    bool unlisted_;                // the new game is not announced (1 from Create)
    std::string hostPassword_;    // the new game's password
    std::vector<std::string> allodNames_; // "lmp_allod_name %d" texts (the game type tabs)
    bool joining_;                 // the client was asking a host last frame
    std::vector<std::string> texts_;      // "lmp_connection %d" texts (22)
    int spare_;                    // 0 from Create; nothing else touches it
    int sortKey_;                  // 0 none; 1..8 the column pairs (odd ascending)
    bool newGameTips_;             // the unlisted and password regions have their tips (DrawList; 0 from Create)
};

// A message box waiting on a master server request: one cancel button, the link to the master server and a progress bar
// sprite. Its result is kBoxMaster (| 2 when done).
class CMasterRequestBox : public CMessageBox {
public:
    // The box (flags 2: one cancel button; format 1), the link connecting to `address`, the
    // bar (a plain sprite, colour 0xFFA06800), and `extra` written to the link's outgoing buffer.
    CMasterRequestBox(CInterface3D* ui, std::string_view titleKey, const char* textKey, std::uint32_t result,
                      const net::NetAddress* address, std::uint32_t extra);
    ~CMasterRequestBox() override;
    // The bar's right edge at 225 + 355 * the received share; the link steps: done (3) hands the reply to the
    // client's server list; the box closes (0x40002) when the link is done or, stepping again, closed (up to
    // three steps a frame).
    void Update() override;

    net::MasterLink link_;
    CUiMesh* bar_;
};

inline MpHeroRecord* SavedHero(int index) { return &g_savedParties->entries_[index]; }
inline int SavedHeroCount() { return static_cast<int>(g_savedParties->entries_.size()); }

// --- In-game escape menu ----------------------------------------------------------------------
// Opened by Esc during a game. Regions, built from the EscMenu layout: 0 save, 1 load, 2 resume, 3 quit
// (message box), 4 options.
class CEscMenu : public CUiContainer {
public:
    explicit CEscMenu(CInterface3D* ui) : CUiContainer(ui) {}
    void CloseScreen() override { OnClose(); }
    void MessageResult(int result) override { OnMessageResult(result); }
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    void OnMouseMove(int flags, int x, int y);
    void OnKeyDown(int key, int repeat, int flags);
    void OnMessageResult(int result);
    // The stone model and the five regions, whose rectangles come from menus.reg.
    void CreateScreen() override;
    // Opens the save window (region 0), the load window (1; neither in multiplayer), resumes (2), asks to quit (3) or opens the
    // options (4).
    void OnLButtonDown(int buttons, int x, int y);
    // The menu closes: its stone model (models_[0]) plays animation 0.
    void OnClose();

    int hovered_;               // region under the mouse, -1 for none
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
class CUiScrollingSurface : public CUiSurface {
public:
    // A surface of format 5 over the layout rectangle (keyed on white); no text, the offsets and the speed 0
    // (startTime_ is left as it was).
    CUiScrollingSurface(CInterface3D* ui, const RECT* rect);
    virtual ~CUiScrollingSurface();
    // Loads the text file `name` of `archive` into the surface (LoadRtf); whether it was found.
    bool Load(CMappedResource* archive, const char* name);
    // Reads the RTF block and lays it out at the surface's width (render_rich_text.h), the offset 0, the text
    // rendered. Returns true.
    bool LoadRtf(const void* data, std::uint32_t size);
    // While the speed is not 0 the offset moves by the game time passed times the speed (truncated) and the
    // text is rendered again; then the surface's own draw.
    void Blit() override;
    // Starts the text at `speed` (0: still), from below the surface when `fromBelow` (the offset minus the surface's height in
    // twips); the end offset 0, the start time the game time.
    void Start(float speed, char fromBelow);
    // Draws the text onto the canvas, white first, its top `offset_` twips above the surface's. The first time the whole
    // text fits above 1000 twips below the surface the offset is kept; once the offset has moved 1000 twips plus the
    // height past it, the speed and the kept offset become 0 (RichEdit's EM_FORMATRANGE end in the original).
    void RenderText();

    float startTime_;
    int offsetX_;               // twips
    int offset_;                // twips scrolled
    float speed_;               // 0 once the text has scrolled through
    int endOffset_;             // offset_ when the end of the text came into view (0: not yet)
    std::unique_ptr<render::text::RichLayout> text_;   // the loaded text laid out (null before LoadRtf)
};

class CCreditsScreen : public CUiContainer {
public:
    CCreditsScreen(CInterface3D* ui, bool fromGame, const char* name);
    ~CCreditsScreen() override;
    void CloseScreen() override { OnClose(); }
    void MessageResult(int result) override { OnMessageResult(result); }
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    void OnLButtonDown(int flags, int x, int y);
    void OnKeyDown(int key, int repeat, int flags);
    // Ends the titles: back to where they were opened from.
    void Close();
    // Opened in a game (fromGame_ 0) the loading screen steps and the music stops; the cursor hidden, the
    // "<name>fin" movies played, "outro.res" opened and its "credits.scr" parsed
    // into lines_ (one record per line: "#scroll" flag, speed, two texts); the scrolling text surface and the
    // header surface, both frozen; the display's flag kept in savedDisplay_ and cleared; the player starts
    // "Movies\<name>.bik".
    void CreateScreen() override;
    // Once the game time passes nextLineAt_ and the text has stopped: both surfaces frozen, then the next
    // line: its text file loaded and started (a "#scroll" line scrolls at its speed under its header, another
    // stays for its speed in seconds), or past the last line finished_. The movie steps (stretched unless the
    // movie flag is 1); when it has ended after the last line: "<name>fout" played, the screen flag cleared
    // and the titles closed as Close() does; otherwise the container's draw.
    BOOL Draw(CSceneRenderer* pass) override;
    // The lines freed, the display's flag restored, the screen flag set and the cursor shown.
    void OnClose();
    // A window above closed: the movie player is closed.
    void OnMessageResult(int result);

    CUiScrollingSurface* textSurface_;   // the scrolling text
    CUiSurface* headerSurface_;
    lib::InPlace<CMappedResource> archive_;   // "outro.res" (built and destroyed by hand)
    std::list<TitleLine> lines_;
    float nextLineAt_;          // game time the current line ends (0 from Build and for scrolls)
    std::list<TitleLine>::iterator currentLine_;   // the next line (end() at the end)
    std::string name_;         // ("Titles")
    CBinkVideo player_;         // the movie player
    bool fromGame_;             // opened in a game (pending value 0xCCCCCCCC) or the menu
    bool finished_;             // the last line has gone (cleared by Build)
    int savedDisplay_;          // the renderer's fog colour while the titles run
};

// --- Main menu ----------------------------------------------------------------------------------
// The main menu (CreateScreen id 2). Regions 0-5 from the menus.reg "MainMenu" rectangles; the stones are the object
// manager's object 2; the world behind it runs the "MainMenu.cam" camera path with rain at night.
class CMainMenu : public CUiContainer {
public:
    explicit CMainMenu(CInterface3D* ui) : CUiContainer(ui) {}
    BOOL Draw(CSceneRenderer* pass) override;
    void CloseScreen() override { Close(); }
    void MessageResult(int result) override { OnMessageResult(result); }
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    // By region: 0 the difficulty question (a message box), 1 load (the save/load window), 2 the exit movie and quit, 3
    // options, 4 the titles (surfaces cleared, opened over the menu), 5 multiplayer (status "reserved_04", the server flag, the
    // hero screen). Then the container's handler.
    void OnLButtonDown(int buttons, int x, int y);
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
    // The map edge fade put back, then two interface calls.
    void Close();
    // A message box's answer (the flow's party step 0 and the server flag cleared first): 0x10001 (the quit
    // box's yes) the exit movie and quit, 0x20001 the intro movie and the flow's state 3, 0xCCCCCCCC (the
    // titles closed) the version line again.
    void OnMessageResult(int result);
    // Hovering a region plays its stone's animation (regions 0-5: 1, 3, 5, 7, 11, 9; off them the rest pose) with the stone
    // sound, once per region; then the container's handler.
    void OnMouseMove(int buttons, int x, int y);
    // The container's handler, then Esc asks to quit (a message box "quit" / "quit_msg", flags 0x10000).
    void OnKeyDown(int key, int repeat, int flags);

    int hovered_;                    // the region under the mouse (-1 none)
    bool rain_;
    bool daylight_;                  // the hour was between 7 and 21 when the menu opened
    float hour_;                     // the time of day at the last update
    float savedEdgeFade_;            // the map edge fade before the menu
};

// Trading between two players (screen 9, Xch_* texts). Multiplayer only.
class CTradeGrid;
class CExchangeScreen : public CItemScreen {
public:
    explicit CExchangeScreen(CInterface3D* ui);
    ~CExchangeScreen() override;
    void CloseScreen() override { Close(); }
    void MessageResult(int result) override { OnMessageResult(result); }
    void ExchangeDone(CParty* partyB, CParty* partyA, CParty* partyC) override {
        OnExchangeDone(partyB, partyA, partyC);
    }
    void TakeItem(CItem* item, int mode) override { MoveOffered(item, mode); }
    bool CanEquip(CItem*, int) override { return CanMove(); }
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    // Builds the window (texture "campinfo", regions 0 accept, 1 take back, 2 leave): see docs/notes/ui.md,
    // "CExchangeScreen::CreateScreen".
    void CreateScreen() override;
    // The item (or help topic) under the mouse from the strip, else the grid, goes to the description panel;
    // the four surfaces get "xch_current_money" with the local party's money and "xch_accept",
    // "xch_money_offer", and while the partner's offer pairs with ours "xch_money_remote" with its money and
    // "xch_remote_header" with "xch_remote_ok" / "_cancel"; the accept / take back buttons are shaded by our
    // offer's acceptance and the third button by the partner's; then the children.
    BOOL Draw(CSceneRenderer* pass) override;
    void OnKeyDown(int key, int repeat, int flags);
    // The exchange went through: the local party's items are released, every item of `partyC`, then of
    // `partyB`, returned into it (ReturnAll), its money set to partyB's - partyA's + partyC's, and the client
    // goes back to the world.
    void OnExchangeDone(CParty* partyB, CParty* partyA, CParty* partyC);
    // The description panel describes nothing and the scratch party is deleted.
    void Close();
    // A message box's answer: nothing.
    void OnMessageResult(int result);
    // The children's update; then, when the client's offer is gone or detached, our offer's items go back to
    // the local party and the client returns to the world; else a changed money field (its changed_ cleared)
    // becomes our offer's money (atoi) and the offer is published.
    void Update() override;
    // The container's handler, then by region: 0 accept (the buy sound and the trade recorded with the local
    // party, when our offer is live and not accepted and its money is within the local party's; else the
    // cancel sound), 1 take the acceptance back (cancel sound and the recorded take-back, when it is
    // accepted), 2 leave (the transit sound and the recorded leave). The event is not marked used.
    void OnLButtonDown(int buttons, int x, int y);
    // Refused by the accept virtual: the cancel sound. Mode 1 (the strip): the put_on sound, `item` moves
    // from the local party into our offer (MoveItem with limit 8, merging) and the offer is published. Mode 5
    // (the grid), only for an item of our offer: the put_off sound, the item moves back to the local party,
    // the strip scrolls to what the party now holds and the offer is published. Other modes nothing.
    void MoveOffered(CItem* item, int mode);
    // Whether an item may move: always.
    bool CanMove();

    CParty* scratch_;                // a party of the screen's own (made by Create, deleted by Close)
    CParty* partnerOffer_;           // the client's parties_[1]
    CParty* ownOffer_;               // the client's parties_[0]
    CUiItemStrip* strip_;            // the local party's items (forwarding strip, mode 1)
    CUiItemStrip* figures_;          // the players (figure strip)
    CTradeGrid* grid_;               // the two offers (mode 5)
    CUiEditField* moneyField_;       // the money offered
    std::vector<std::string> names_;           // the figure strip's names
};
