#pragma once

// Interface screens in 00610000-00630000 (see docs/notes/ui.md, "Screens"): the camp's item strips,
// the multiplayer hero screen, the in-game escape menu, the credits and the exchange window.
// Names provisional. Every screen is a CUiWindow (ui.h) extended with its own fields; the classes
// here hold the base window as their first member and name only the fields the rewritten code
// uses.
//
// Input handlers are called on the window's second vtable sub-object at +0x94 (CInterface3D's
// dispatch at 005F76A0-005F78C0 calls through `window + 0x94`), so they receive the window plus
// 0x94 in ECX. The registered entry points are __fastcall shims that convert that pointer and call
// the window method.
//
// Handler arguments (as the interface manager passes them):
//   mouse: (button flags, x, y), x and y in screen pixels;
//   key down: (virtual-key code, repeat count, scan code and flags);
//   wheel: (key flags, wheel delta, x, y).

#include "ui.h"

#include <cstddef>
#include <cstdint>

// Offset of the input sub-object (the second vtable) inside every window.
constexpr std::ptrdiff_t kUiInputOffset = 0x94;

// The input sub-object at window+0x94: the base screen class's handlers (vtable 00741008 subclass
// of CUiWindow), not rewritten. Derived handlers call them first or last, as the original does.
struct UiInput {
    void BaseLButtonDown(int flags, int x, int y);   // 00601110
    void BaseLButtonUp(int flags, int x, int y);     // 006011F0
    void BaseMouseMove(int flags, int x, int y);     // 006012D0
    void BaseKeyDown(int key, int repeat, int flags);   // 00601820
};

// The window an input sub-object belongs to, and back.
template <typename T>
T* WindowFromInput(void* input) {
    return reinterpret_cast<T*>(static_cast<char*>(input) - kUiInputOffset);
}
inline UiInput* InputOf(void* window) {
    return reinterpret_cast<UiInput*>(static_cast<char*>(window) + kUiInputOffset);
}

// Window fields shared by every screen (inside CUiWindow's unknown bytes).
// +0x9C: set by a handler that consumed the event (input sub-object +8); the interface manager
// then stops passing it on.
inline bool& UiHandled(void* window) { return *reinterpret_cast<bool*>(static_cast<char*>(window) + 0x9C); }

// CInterface3D fields not named in ui.h.
// +0x0C: the input sub-object that captured the mouse (scroll arrows held down), or null.
inline void*& UiCapture(CInterface3D* ui) { return *reinterpret_cast<void**>(reinterpret_cast<char*>(ui) + 0x0C); }
// +0x7D: a shift key is held (set by the key-down dispatch 005F78C0).
inline bool UiShiftDown(const CInterface3D* ui) { return reinterpret_cast<const char*>(ui)[0x7D] != 0; }

// What the original stores as the capture: the input sub-object of `window`, or null for a null
// window (LEA EAX,[ESI-0x94]; NEG; SBB; AND ESI).
inline void* CaptureValue(void* input) {
    return static_cast<char*>(input) - kUiInputOffset != nullptr ? input : nullptr;
}

// Not rewritten.
namespace uiorig {
// 005F7E60. Plays an interface sound (thiscall on CInterface3D; the object is not read).
void PlayUiSound(CInterface3D* ui, const char* name, int flags);
// 00581660. Leaves the running game for the main menu (the game object 007AFE20 in ECX).
void LeaveGame();
} // namespace uiorig

static_assert(sizeof(CUiWindow) == 0x148);

// An MFC CPtrArray-like array (vtable, data, size, capacity, growth) with zeroed new slots.
struct UiItemArray {
    void* vtable_;
    char** data_;
    int size_;
    int capacity_;
    int growBy_;
    // 0062A390 (not rewritten; an MFC SetSize instantiation). `growBy` -1 keeps the step.
    void SetSize(int newSize, int growBy);
};
static_assert(sizeof(UiItemArray) == 0x14);

// The item predicates of the camp screens (005EE050-005EE130, thiscall on the owning screen,
// which they do not read; RET 4; ui_screens.cpp). Loot items (type 0x3007, CItemLoot) are sorted
// by kind (+0x4C) and record type (+0x50).
struct UiItemKinds {
    bool IsPart(const char* item);       // 005EE0D0: kind 2, record type 0-2 (blueprint parts)
    bool IsKindOne(const char* item);    // 005EE110: kind 1 (a material)
    bool IsSpell(const char* item);      // 005EE050: kind 2, record type 3 (the spell table)
    bool IsModifier(const char* item);   // 005EE080: kind 2, record type 4 (spell modifier table)
    BOOL IsSpellContainer(const char* item);   // 005EE0B0: type 0x3008 (all of EAX)
    // 005EE130: an item the item constructor can take apart: a weapon (0x3004), type 0x3005, or
    // armour (0x3006) whose prototype's material class is not "none"; never when the prototype
    // (006A0110) exists with its +0x68 flag clear.
    bool IsBuildable(const char* item);
};

// The fields of an item prototype record (006A0110's result) the predicates read.
struct ItemPrototypeRecord {
    std::uint8_t unknown_00[0x10];
    const char* materialClass_;      // +0x10 "none" for items without a material
    std::uint8_t unknown_14[0x68 - 0x14];
    bool buildable_;                 // +0x68
};
static_assert(offsetof(ItemPrototypeRecord, materialClass_) == 0x10);
static_assert(offsetof(ItemPrototypeRecord, buildable_) == 0x68);

// --- Camp item strip ------------------------------------------------------------------------
//
// The horizontal strip of items (or spells) at the bottom (y 500) or top (y 0) of the camp screens:
// six tab regions choose a category, two arrows scroll it, the strip itself (region 8) shows
// `visible_` slots. Vtables 007419E8/00741A1C (strip) and 00741948/0074197C (strip that forwards
// item clicks to its owner); created by the camp screen (0060C870) and the exchange window
// (0062C250). Regions: 0/1 left/right arrows, 2-7 tabs, 8 the strip.
class CUiItemStrip {
public:
    // 00620380 (RET 4; ui_item_strip.cpp; from the camp's and the hero screen's strip setup
    // 00621160 / 00629A40). Lays the strip out along the top edge (`top`: y 0, scale 8) or the
    // bottom (y 500, scale 4): the scroll state cleared, nine regions (arrows, six tab buttons, the
    // items), the ends' and tab buttons' sprites, the highlight frame, the background cells; tab 5.
    void Build(char top);
    // 00620960. Selects tab `tab`: settles the current tab's position to a whole slot, highlights
    // the tab's button and stops scrolling.
    void SetTab(int tab);
    // 00621420. Chooses what the tabs sort by (0 items, 1 spells, 2 armour) and loads their
    // tooltips; every tab's position goes back to the start.
    void SetFilter(int filter);
    // 00621510 / 00621530. Tab selection used by the camp screen while tabs are enabled (global
    // 0079BE84): select `tab`; or cycle within [first, first + count).
    void SelectTab(int tab);
    void CycleTab(int first, int count);
    // 006218F0, 006219A0, 00621A80. The tab (0-5) an item belongs to under each filter.
    int ItemTab(const char* item);
    int SpellTab(const char* item);
    int ArmorTab(const char* item);
    // 006212F0. Virtual slot 26: rebuilds the visible list from the source list for the current
    // filter and tab (tab 5 of filter 0 shows everything).
    void RebuildList();
    // 00621570 (RET 4; ui_screens.cpp). Rebuilds the list as RebuildList does, first switching to
    // `item`'s tab (SetTab, while the tab keys are enabled; not for filter 0's tab 5), and scrolls
    // the tab so the last entry `item` is the same as (CItem slot 14) is visible: its position
    // moves into [index - 6, index]. The scroll stops (pending and speed 0); count_ is not
    // updated. Null does nothing.
    void ScrollToItem(char* item);
    // The tab of `item` under the current filter (0 for an unknown filter), as both rebuilds
    // choose it.
    int TabOf(const char* item);
    // 00620960 through its original address (its verify wrapper runs).
    void CallSetTab(int tab);
    // 00621F30. Virtual slot 25: the item under the screen point (x, y), or null; sets the cursor
    // for the strip's action mode.
    char* ItemAt(int x, int y, int* cursor);
    // 00621B50 (slot 20 of the forwarding strip's vtable 0074197C; ui_item_strip.cpp). Draws the
    // strip: the end faces dimmed (sprite +0xCC 0.195) where it can scroll on, the background cells
    // shifted by the current tab's fractional position, the window's sprites, eight item cells
    // through the owner (005ECA10), the stack counts (not 1) on surface 0 and, unless +0x1B0 is 9,
    // the owner's slot 54 number of each item its slot 43 takes on surface 1. Returns 1.
    BOOL Draw(int pass);
    // 00620A50 (slot 24, every frame; ui_item_strip.cpp; vtables 0074197C, 00741A1C, 00741BF4).
    // Scrolls: a held arrow (+0x154) adds one slot to the pending scroll at once and one more
    // every 0.1 s (+0x158 the timer); the pending scroll (+0x174) moves the current tab's
    // position at a speed (+0x178) that accelerates (15 slots/s^2, at most 7) and eases out within
    // half a slot, settling on a whole slot; then the list is rebuilt (slot 26) and the position
    // kept within [0, count - 7].
    void Update();

    // Input handlers (input sub-object).
    void OnLButtonDown(int flags, int x, int y);   // 00620E10
    void OnLButtonUp(int flags, int x, int y);     // 00621010
    void OnMouseMove(int flags, int x, int y);     // 00620F10
    BOOL OnWheel(int keys, short delta, int x, int y);   // 006210F0
    void OnLButtonDownForward(int flags, int x, int y);  // 006212A0 (the forwarding subclass)

    // Settles the pending scroll on release (inlined twice by the original).
    void SettleScroll();
    // Not rewritten: virtual slots 25 and 26 called through the vtable.
    char* CallItemAt(int x, int y, int* cursor);
    void CallRebuildList();

    CUiWindow window;           // the strip's own fields start right after it, at +0x148
    int actionMode_;           // +0x148 what clicking an item does (1 equip, 2, 3; cursor 0x16-0x18)
    CUiWindow* owner_;          // +0x14C the screen that owns the strip (item clicks, predicates)
    int tab_;                   // +0x150 0-5
    int scrollDirection_;       // +0x154 0 none, 1 left arrow held, 2 right arrow held
    float scrollTimer_;         // +0x158
    float position_[6];         // +0x15C scroll position of each tab, in slots
    float pending_;             // +0x174 scroll still to apply, in slots
    float velocity_;            // +0x178
    int unknown_17C;            // +0x17C -1
    int top_;                   // +0x180 y of the strip: 500 (bottom) or 0 (top)
    float scale_;               // +0x184
    int count_;                 // +0x188 visible list size after the last rebuild
    bool tabsEnabled_;          // +0x18C
    std::uint8_t unknown_18D[3];
    int hovered_;               // +0x190 -1
    int filter_;                // +0x194 0 items, 1 spells, 2 armour
    UiItemArray list_;          // +0x198 the visible items
    UiPtrArray<char>* source_;  // +0x1AC all the items (data, size)
    int slots_;                 // +0x1B0 9
};
static_assert(offsetof(CUiItemStrip, actionMode_) == 0x148);
static_assert(offsetof(CUiItemStrip, tab_) == 0x150);
static_assert(offsetof(CUiItemStrip, position_) == 0x15C);
static_assert(offsetof(CUiItemStrip, pending_) == 0x174);
static_assert(offsetof(CUiItemStrip, count_) == 0x188);
static_assert(offsetof(CUiItemStrip, filter_) == 0x194);
static_assert(offsetof(CUiItemStrip, list_) == 0x198);
static_assert(offsetof(CUiItemStrip, source_) == 0x1AC);
static_assert(offsetof(CUiItemStrip, slots_) == 0x1B0);

// --- Camp screen ------------------------------------------------------------------------------
//
// The camp (vtables 007408E8/0074091C, 0x260 bytes; Create 0060C870, which Rebirth patches): the
// party's equipment, trade, repair and the item and spell constructors, with three item strips.
// Also opened by the multiplayer hero screen (region 8) for a hero's inventory.
class CCampScreen {
public:
    // 00610020. Virtual slot 45: over the party's heroes, the best skill in spell school `school`
    // (rounded to nearest) and the most mana units (maximum mana / period, truncated).
    void BestPartySkill(int school, int* skill, int* mana);
    // 006110F0. Virtual slot 44: shows the strip tabs that go with camp page `page` (1-21).
    void ShowPageTabs(int page, int unused);

    CUiWindow window;
    std::uint8_t unknown_148[0x1E0 - 0x148];
    CUiItemStrip* itemStrip_;   // +0x1E0
    std::uint8_t unknown_1E4[0x1EC - 0x1E4];
    CUiItemStrip* spellStrip_;  // +0x1EC
    CUiItemStrip* partStrip_;   // +0x1F0
};
static_assert(offsetof(CCampScreen, itemStrip_) == 0x1E0);
static_assert(offsetof(CCampScreen, partStrip_) == 0x1F0);

// A party hero's statistics (UnitStats at +0x20 of a 0x784-byte hero record) as the camp reads
// them: the skill in a spell school rounded like FISTP, and the whole "mana units" (the mana
// maximum stored as float, divided by the period, truncated).
int HeroSchoolSkill(void* stats, int school);
int HeroManaUnits(void* stats);
// Whether a hero with `stats` can use the spell `spell` (+0x50 required skill, +0x40 mana cost).
bool HeroCanUseSpell(void* stats, char* spell);

// --- Multiplayer hero screen ----------------------------------------------------------------
//
// Opened by the main menu's Multiplayer button (region 5 of 00632D70; vtables 00742118/0074214C,
// 0x220 bytes). Modes (+0x190, set by 00615D30): 9 hero list, 10 new hero (portrait, name and
// voice), 11 new hero's statistics. The heroes are records of 0x74 bytes in the vector at 007C82A4.
class CMpHeroScreen {
public:
    // 00618FF0. Keeps the selected hero in the list and visible (ten rows), then shows it.
    void ClampHeroSelection();
    // 00619090. The same for the voice list (eleven rows), then shows the voice's name.
    void ClampVoiceSelection();
    // 006193C0. Virtual slot 53: the weight carried by the hero being made (weapon, armour, spell
    // container and the quick items), left unrounded in ST0.
    double EquipmentWeight();
    // 00618980. Virtual slot 42 (what an item strip calls with its action mode): with mode 0,
    // equips a weapon, armour or spell container; then refreshes.
    void OnItemChosen(char* item, int mode);
    // 00616900. Virtual slot 51: the local player's first hero (+0x30 of the player data).
    char* GetHero();
    // 006188E0. Virtual slot 43: whether the hero can take `item`; spell containers need the
    // spell's skill and mana (the camp's own version is 006100E0, not rewritten).
    bool CanEquip(char* item, int mode);

    void OnLButtonUp(int flags, int x, int y);   // 006177D0
    // 00617050 (input slot 2; ui_hero_screen_click.cpp). Nothing while a hero is being renamed;
    // after the base handler, by mode: the hero list (9: back to the main menu, continue, the
    // rotate arrows, new hero, rename, delete (a question), the hero's inventory (the camp screen
    // with fromHeroScreen_), list rows), the new hero (10: back, create, the arrows, defaults,
    // voice rows) and its statistics page (11: back, next, take the equipment back).
    void OnLButtonDown(int flags, int x, int y);

    // Called through the vtable, as the original does.
    char* Hero();                       // slot 51 (+0xCC)
    void Refresh();                     // slot 47 (+0xBC), 00618850 here
    // Not rewritten.
    void ShowHero(int index);           // 00616910
    void DrawVoices();                  // 00616560

    CUiWindow window;
    std::uint8_t unknown_148[0x190 - 0x148];
    int mode_;                  // +0x190 9, 10, 11
    int heroIndex_;             // +0x194 selected hero
    void* heroScroll_;          // +0x198 scroll bar of the hero list
    int heroTop_;               // +0x19C first row shown
    std::uint8_t unknown_1A0[4];
    char* weapon_;              // +0x1A4
    char* armor_;               // +0x1A8
    char* spellContainer_;      // +0x1AC
    void* statsPanel_;          // +0x1B0 (CHeroStatsPanel; 006232D0 sets its hero)
    void* weaponPanel_;         // +0x1B4 shows the weapon and the armour
    void* spellPanel_;          // +0x1B8
    std::uint8_t unknown_1BC[0x1C4 - 0x1BC];
    void* tradePanel_;          // +0x1C4 (CTradePanel; 006255A0)
    void** defaults_;           // +0x1C8 its first entry goes to slot 52 for a new hero
    std::uint8_t unknown_1CC[0x1E0 - 0x1CC];
    float turn_;                // +0x1E0 the preview's turn
    int rotating_;              // +0x1E4 0, 1 or 2 while a rotate arrow is held
    std::uint8_t* renameEdit_;  // +0x1E8 the hero list's rename edit window (+0x179: editing)
    void* nameEdit_;            // +0x1EC the new hero's name edit window (0065C290 sets its text)
    void* voiceScroll_;         // +0x1F0
    std::uint8_t unknown_1F4[0x210 - 0x1F4];
    char** voices_;             // +0x210 STLport vector of {CString file, CString name} (8 bytes)
    int voiceTop_;              // +0x214
    int voiceIndex_;            // +0x218
    int voiceSound_;            // +0x21C
};
static_assert(offsetof(CMpHeroScreen, rotating_) == 0x1E4);
static_assert(offsetof(CMpHeroScreen, renameEdit_) == 0x1E8);
static_assert(offsetof(CMpHeroScreen, tradePanel_) == 0x1C4);
static_assert(offsetof(CMpHeroScreen, mode_) == 0x190);
static_assert(offsetof(CMpHeroScreen, heroTop_) == 0x19C);
static_assert(offsetof(CMpHeroScreen, weapon_) == 0x1A4);
static_assert(offsetof(CMpHeroScreen, weaponPanel_) == 0x1B4);
static_assert(offsetof(CMpHeroScreen, voiceScroll_) == 0x1F0);
static_assert(offsetof(CMpHeroScreen, voices_) == 0x210);
static_assert(sizeof(CMpHeroScreen) == 0x220);

// The multiplayer heroes (records of 0x74 bytes; +0x30 CString name, +0x70 CString file).
inline const RecordTable& MpHeroes() { return *reinterpret_cast<const RecordTable*>(0x007C82A4); }
constexpr int kMpHeroSize = 0x74;

// --- In-game escape menu ----------------------------------------------------------------------
//
// Opened by Esc during a game (vtables 00741270/007412A4). Regions, built from the EscMenu layout
// (0062AB20): 0 save, 1 load, 2 resume, 3 quit (message box), 4 options.
class CEscMenu {
public:
    void OnMouseMove(int flags, int x, int y);    // 0062AD10
    void OnKeyDown(int key, int repeat, int flags);   // 0062B300
    // 0062ACC0. Virtual slot 27: a message box this menu opened was closed with `result`.
    void OnMessageResult(int result);
    // 0062AB20 (slot 25, the menu's Create): the stone model and the five regions, whose
    // rectangles come from menus.reg (007C80A8..007C80E8; ui_layouts.cpp).
    void Build();
    // 0062AE90 (input slot 2): opens the save window (region 0), the load window (1; neither in
    // multiplayer), resumes (2), asks to quit (3) or opens the options (4) (ui_layouts.cpp).
    void OnLButtonDown(int buttons, int x, int y);

    CUiWindow window;
    std::uint8_t unknown_148[0x164 - 0x148];
    int hovered_;               // +0x164 region under the mouse, -1 for none
};
static_assert(offsetof(CEscMenu, hovered_) == 0x164);

// --- Credits ------------------------------------------------------------------------------------
//
// The titles window (vtables 00740C08/00740C3C, 0x1FC bytes; main menu region 4).
class CCreditsScreen {
public:
    void OnLButtonDown(int flags, int x, int y);   // 0062BFE0
    void OnKeyDown(int key, int repeat, int flags);   // 0062C010
    // Ends the titles: back to where they were opened from.
    void Close();
    // 0062B7D0 (slot 25, the window's Create; ui_layouts.cpp). Opened in a game (fromGame_ 0) the
    // loading screen steps and the music stops; the cursor hidden, the "<name>fin" movies played,
    // font smoothing switched off, "outro.res" opened and its "credits.scr" parsed into lines_
    // (one record per line: "#scroll" flag, speed, two texts); the scrolling text surface (+0x164)
    // and the header surface (+0x168), both frozen; the display's +0x14 kept in savedDisplay_ and
    // cleared; the player +0x1D8 starts "Movies\<name>.bik".
    void Build();

    CUiWindow window;
    std::uint8_t unknown_148[0x164 - 0x148];
    CUiSurface* textSurface_;   // +0x164 the scrolling text (0x58 bytes, constructor 0065ED30)
    CUiSurface* headerSurface_; // +0x168
    int fontSmoothing_;         // +0x16C passed (as uiParam) to SPI_GETFONTSMOOTHING, never written
    std::uint8_t archive_[0x1C8 - 0x170];   // +0x170 CMappedResource ("outro.res")
    void* lines_;               // +0x1C8 STLport list of TitleLine (its sentinel node)
    int unknown_1CC;            // +0x1CC cleared by Build
    void* currentLine_;         // +0x1D0 list iterator: the first line (the sentinel without a script)
    const char* name_;          // +0x1D4 CString ("Titles")
    std::uint8_t player_[0x1F4 - 0x1D8];    // +0x1D8 the movie player (constructor 005DB340)
    bool fromGame_;             // +0x1F4 opened in a game (pending value 0xCCCCCCCC) or the menu
    bool unknown_1F5;           // +0x1F5 cleared by Build
    std::uint8_t unknown_1F6[2];
    int savedDisplay_;          // +0x1F8 the display's (00500960) +0x14 while the titles run
};
static_assert(offsetof(CCreditsScreen, textSurface_) == 0x164);
static_assert(offsetof(CCreditsScreen, lines_) == 0x1C8);
static_assert(offsetof(CCreditsScreen, fromGame_) == 0x1F4);
static_assert(offsetof(CCreditsScreen, savedDisplay_) == 0x1F8);
static_assert(sizeof(CCreditsScreen) == 0x1FC);

// A line of the titles' script (the value of a lines_ node, after next / prev).
struct TitleLine {
    bool scroll;                // +0 the first word is "#scroll"
    std::uint8_t unknown_1[3];
    float speed;                // +4 the second word (atof)
    const char* text;           // +8 CString: the fourth word
    const char* style;          // +0xC CString: the third word
};
static_assert(sizeof(TitleLine) == 0x10);

// --- Main menu ----------------------------------------------------------------------------------
//
// The main menu window (input vtable slot 2 at 0074065C).
class CMainMenu {
public:
    // 00632D70 (input slot 2; ui_layouts.cpp). By region: 0 the difficulty question (a message
    // box, vtables 0074225C / 00742228), 1 load (the save/load window), 2 the exit movie and quit,
    // 3 options, 4 the titles (surfaces cleared, opened over the menu), 5 multiplayer (status
    // "reserved_04", the server flag, the hero screen); each window is built inline as the
    // original does. Then the container's handler.
    void OnLButtonDown(int buttons, int x, int y);

    CUiWindow window;
};

// --- Exchange window ------------------------------------------------------------------------
//
// Trading between two players (xch_* texts; Create 0062C250 is its virtual slot 25).
class CExchangeScreen {
public:
    void OnKeyDown(int key, int repeat, int flags);   // 0062D090

    CUiWindow window;
};
