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

#include "bink_video.h"
#include "ui.h"
#include "ui_item_info.h"
#include "net_classes.h"

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CItemInfoPanel;
class CMappedResource;
class CParty;

// Offset of the input sub-object (the second vtable) inside every window.
constexpr std::ptrdiff_t kUiInputOffset = 0x94;

// The input sub-object at window+0x94: the base screen class's handlers (vtable 00741008 subclass
// of CUiWindow), not rewritten. Derived handlers call them first or last, as the original does.
struct UiInput {
    void BaseLButtonDown(int flags, int x, int y);   // 00601110
    void BaseLButtonUp(int flags, int x, int y);     // 006011F0
    void BaseMouseMove(int flags, int x, int y);     // 006012D0
    void BaseKeyDown(int key, int repeat, int flags);   // 00601820
    void BaseKeyUp(int key, int repeat, int flags);     // 006019A0
    void BaseChar(int key, int repeat, int flags);      // 006018E0
    int BaseWheel(int flags, int delta, int x, int y);  // 00601730 (RET 0x10; returns 1)

    // The input side's slot 0 of a window (SUB ECX,0x94; JMP to the window's deleting destructor,
    // ui_window_delete.cpp): `Delete` is that window class's own method; returns the window.
    template <auto Delete>
    auto DeleteWindow(unsigned flags);
};

// The class a member function pointer belongs to.
template <typename F>
struct MemberClass;
template <typename R, typename C, typename... A>
struct MemberClass<R (C::*)(A...)> {
    using type = C;
};

// The window an input sub-object belongs to, and back.
template <typename T>
T* WindowFromInput(void* input) {
    return reinterpret_cast<T*>(static_cast<char*>(input) - kUiInputOffset);
}
inline UiInput* InputOf(void* window) {
    return reinterpret_cast<UiInput*>(static_cast<char*>(window) + kUiInputOffset);
}

template <auto Delete>
auto UiInput::DeleteWindow(unsigned flags) {
    using Window = typename MemberClass<decltype(Delete)>::type;
    return (WindowFromInput<Window>(this)->*Delete)(flags);
}

// The window's input side's handled flag (+0x9C): set by a handler that consumed the event; the
// interface manager then stops passing it on.
inline bool& UiHandled(void* window) { return static_cast<CUiWindow*>(window)->Input()->handled_; }

// The input sub-object that captured the mouse (scroll arrows held down), or null.
inline void*& UiCapture(CInterface3D* ui) { return ui->mouseCapture_; }
// A shift key is held (set by the key-down dispatch 005F78C0).
inline bool UiShiftDown(const CInterface3D* ui) { return ui->shiftDown_ != 0; }

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

// An MFC CArray of item pointers with zeroed new slots. Its SetSize is the instantiation's
// (0062A390, not rewritten; `growBy` -1 keeps the step), through UiItemsSetSize (ui_screens.cpp).
using UiItemArray = mfc::CArray<char*>;
void __fastcall UiItemsSetSize(UiItemArray* items, void*, int newSize, int growBy);

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
    std::uint8_t unused_00[0x10];
    const char* materialClass_;      // +0x10 "none" for items without a material
    std::uint8_t unused_14[0x68 - 0x14];
    bool buildable_;                 // +0x68
};
static_assert(offsetof(ItemPrototypeRecord, materialClass_) == 0x10);
static_assert(offsetof(ItemPrototypeRecord, buildable_) == 0x68);

// An STLport vector<CString> (the figure names), its elements as their data pointers.
using MpStringVector = stlp::vector<const char*>;

// --- Camp item strip ------------------------------------------------------------------------
//
// The horizontal strip of items (or spells) at the bottom (y 500) or top (y 0) of the camp screens:
// six tab regions choose a category, two arrows scroll it, the strip itself (region 8) shows
// `visible_` slots. Vtables 007419E8/00741A1C (strip) and 00741948/0074197C (strip that forwards
// item clicks to its owner); created by the camp screen (0060C870) and the exchange window
// (0062C250). Regions: 0/1 left/right arrows, 2-7 tabs, 8 the strip.

class CUiItemStrip {
public:
    // 00621160 (RET 8; ui_item_strip_setup.cpp; the camp's three strips and the exchange window's,
    // built inline by their Creates). The background texture "Inventory02" (`top`) or
    // "Inventory01", the strip built (Build through its address), the arrows' tips 0x4FB0 /
    // 0x4FB1, two text surfaces (x 50..750, y top_ + 15..35 and top_ + 65..85), nine slots and the
    // source list.
    void Setup(UiPtrArray<char>* source, char top);
    // 00629A40 (RET 0xC; ui_item_strip_setup.cpp). The figure strips (vtable 00741BF4: the hero
    // screen's two, the exchange window's): the name list, the texture "InventoryM01", Build, the
    // arrows' tips 0x9DD0 / 0x9DD1 unless `noArrowTips`, no tabs and the six tab buttons' sprites
    // (scene objects 2..7) hidden.
    void SetupFigures(MpStringVector* names, char top, char noArrowTips);
    // 00621F20 (RET 4; ui_small_members.cpp; the camp's Create and its page switch). slots_ = `slots`.
    void SetSlots(int slots);
    // 006157C0 (slot 25 of the figure strips 00741BF4, RET 0xC). No item under the point: null,
    // `topic` not written.
    void* FigureItemAt(int x, int y, int* topic);
    // The figure strips' own slots (vtable 00741BF4; ui_item_strip_setup.cpp). 00629AF0 (slot 20,
    // RET 4, returns 1): the end faces dimmed where it can scroll on, the background and cells
    // placed by the current tab's fraction, the frame (scene object 8) on selected_ when it is in
    // view, the window's sprites, then each visible name's hero face through the owner (005ED7B0).
    // 00629AD0 (slot 26): count_ = the number of names. 00629DB0 (input slot 2, RET 0xC; the
    // result is not read): the strip's own click, then on the items (region 8) the figure under
    // the point (as ItemAt) picked: the "on_off" sound and the owner's slot 52 with its name.
    BOOL DrawFigures(int pass);
    void CountFigures();
    void OnFigureClick(int buttons, int x, int y);
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

    // 0060E2B0 (ui_window_delete.cpp). The forwarding strip's destructor body (vtable 0074197C):
    // the visible list's storage, then the window's. The plain strip's (0060E280) frees nothing of
    // its own.
    void DestroyForwarding();
    // 0060E290 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyForwarding, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CUiItemStrip* ForwardingStripDelete(unsigned flags);

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
    int selected_;              // +0x17C -1; the hero screen's figure strips (vtable 00741BF4): the chosen figure
    int top_;                   // +0x180 y of the strip: 500 (bottom) or 0 (top)
    float scale_;               // +0x184
    int count_;                 // +0x188 visible list size after the last rebuild
    bool tabsEnabled_;          // +0x18C
    std::uint8_t unused_18D[3];
    int hovered_;               // +0x190 -1
    union {
        int filter_;                // +0x194 0 items, 1 spells, 2 armour
        MpStringVector* figures_;   // +0x194 the figure strips (vtable 00741BF4): the figure names
    };
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
    std::uint8_t unused_148[0x1E0 - 0x148];
    CUiItemStrip* itemStrip_;   // +0x1E0
    std::uint8_t unused_1E4[0x1EC - 0x1E4];
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

// A voice of the hero screen (8 bytes: two CStrings, kept as their pointers).
struct MpVoiceEntry {
    // 00615800. The destructor: the name, then the file.
    void Destroy();

    const char* file;           // +0 CString
    const char* name;           // +4 CString
};
static_assert(sizeof(MpVoiceEntry) == 8);
using MpVoiceVector = stlp::vector<MpVoiceEntry>;
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
    // 00618A10 (input slot 9, RET 0xC; ui_hero_screen_click.cpp). After the container's handler,
    // unless a child used the key: Enter ends a renaming (FinishRename), else continues (mode 9),
    // creates the hero (10) or goes on to the next page (11); Esc ends a renaming without keeping
    // it (the list redrawn, 006167C0), else goes back to the main menu (9, as region 0), to the
    // hero list (10) or to the new hero (11, as region 0); PgUp / PgDn / Up / Down move the hero
    // selection by 10 / 1 (mode 9) or the voice by 11 / 1 (mode 10, the voice played). A key used
    // is marked (other modes leave the paging keys unused).
    void OnKeyDown(int key, int repeat, int flags);
    // Enter while renaming: the hero's name becomes the text before its " | " plus " | " and the
    // typed text (trimmed; nothing added when empty); the heroes' transfer sent (00662AC0 with
    // 007C82A0); the edit window put away and the hero shown again.
    void FinishRename();
    // The rename edit window put away (its surface hidden, disabled, editing off).
    void CloseRenameEdit();
    // Back from the statistics page to the new hero (mode 10).
    void BackToNewHero();

    // Called through the vtable, as the original does.
    char* Hero();                       // slot 51 (+0xCC)
    void Refresh();                     // slot 47 (+0xBC), 00618850 here
    void VirtualChooseFigure(const char* prototype);   // slot 52 (+0xD0), ChooseFigure here
    // 00616910 (RET 4; ui_hero_screen.cpp). The local player's first record handed to the client
    // world; hero `index` (when in the list; else none, -1) copied into the local party and made
    // current; the preview rebuilt (00616E20 with 1); the hero's unit re-armed (four slots
    // cleared, its weapons given, the equipment sent, the chosen weapon shown); the turn reset and
    // the list redrawn.
    void ShowHero(int index);
    // 00616E20 (RET 4; ui_hero_screen.cpp). The preview cleared; the local player (given a "Hero" of
    // "Human Hero" when it has no record) hands its first record to the client world (00566F70
    // with `flag`); the preview becomes the client unit with that record's id, its prototype
    // index stored; when there is one: skill animation 0x21 (if any) set to start at a random
    // frame (timeGetTime at 15 frames a second minus random % 51), the figure shows no weapon,
    // refreshed, its animation restarted at the current frame - 1, the record's statistics set
    // and the controller's slot 31 called.
    void MakePreview(int flag);
    // 00616AC0 (slot 52, RET 4; ui_hero_figure.cpp). The new hero takes figure (unit prototype)
    // `prototype`: the local player's first record handed to the client world and its strength,
    // dexterity and intelligence and height kept (25, 25, 25, 0.7 without one); the party cleared
    // and given a "Hero" of that prototype with its starting equipment (006625B0); the attributes
    // (base and current) reset to 25 and the height to 0.7 on the hero list (mode 9), else the kept
    // ones; each figure strip selects the prototype when it lists it (-1 otherwise); the
    // equipment panels show the first weapon, quick item and spell; the preview, the statistics
    // panel's and the trade panel's hero rebuilt; text_ = prototype; the voice list of the strip
    // that lists it (voiceLists_[1] for names_, else [0]) from row 0, its first file the hero's
    // voice (text 6); the voice scroll bar's range and position 0; on the new hero page the
    // voices drawn.
    void ChooseFigure(const char* prototype);
    // 00616560. The voices' surface (2): the "lmp_voices" title and eleven rows from voiceTop_,
    // the selection frame (scene object 2) on the chosen one.
    void DrawVoices();
    // 006167C0. The hero list's surface (0): ten rows of names from heroTop_, the selection frame
    // (scene object 2) on the selected hero.
    void DrawHeroList();
    MpVoiceVector& Voices() { return *reinterpret_cast<MpVoiceVector*>(voices_); }
    // The three above through their original address (their wrappers run; ui_screens.cpp).
    void CallShowHero(int index);
    void CallDrawVoices();
    void CallDrawHeroList();

    // 006159E0 (slot 20, RET 4; ui_hero_screen.cpp). On the statistics page (11) the experience
    // lines on surface 2; then what is under the mouse for the description panel (the equipment
    // panels' item or help topic, the trade panel's or the statistics' entry, on the new hero's
    // page the voice rows' "lmp_select_voice"), the children drawn and the hero previewed (list
    // with a hero selected at depth 553, new hero at 400). Returns 1.
    BOOL Draw(int pass);
    // 00617820 (slot 24). The children's update; a held rotate arrow turns the preview by twice the
    // frame time (modulo 007C7F10); the two scroll bars' moves redraw the hero list and the voices;
    // on the new hero's page the create button (scene object 9) is bright only when the three
    // attributes add up to 00741C98's points and the name is not empty.
    void Update();
    // 00615850 (slot 26). The heroes saved (006632B0 on 007C82A0), the local player's unit records
    // handed to 005789E0, the preview cleared and the description panel emptied.
    void Close();
    // 00633260 (the destructor body; slot 0 00633240 deletes): the two voice lists' entries and
    // storage, the CString +0x1F4, the two string vectors, the texture and model caches (objects
    // released, nodes erased), then the container's body (00600BA0).
    void Destroy();
    // 00633240 (slot 0, the deleting destructor; ui_window_delete.cpp). Destroy, then the memory
    // freed when bit 0 of `flags` is set; returns this.
    CMpHeroScreen* HeroScreenDelete(unsigned flags);
    // The level-up and experience lines of the statistics page (inlined by Draw).
    void DrawExperience();
    // 00618060 (ui_hero_screen_click.cpp). Region 6 of the list: the rename edit window enabled and
    // editing; its text is the selected hero's name after its "|" (left-trimmed; empty without one),
    // and surface 3 shows the name before it (right-trimmed) plus " | ". Nothing without a selection.
    void Rename();
    // 00619150. The hero's voice: the sound playing stopped, then the voice (hero text 6) looked up
    // as a unit prototype (007AF364), the prototype's name in the sound sets (007C8A6C); one of the
    // set's entries with flags 0x2282 and a name, from a random one on, played (voiceSound_).
    void PlayVoice();
    // 006183B0. Enter / region 1 of the list: with a hero selected the heroes are saved and the
    // connection screen opened on top (built inline); otherwise a message box ("lmp_..." texts).
    void Continue();
    // 00618850 (slot 47; called through Refresh()). With a preview figure: the hero's statistics
    // slots restored, its size factors (+0x724 / +0x728), the figure's scale, the figure's unit
    // statistics and its controller's slot 31; then the statistics panel redrawn.
    void RefreshHero();
    // 006186F0. Region 1 / Enter of the new hero: when the three attributes add up to 00741C98's
    // points (equal or unordered) and a name is typed, the first weapon, +0x740 entry and spell
    // entry shown, the name given to the hero and the statistics panel, refreshed, mode 11;
    // otherwise the cancel sound.
    void CreateHero();
    // 00617E40 (ui_hero_screen_click.cpp). The selected hero deleted: its file (folder + the
    // record's file name) removed, the later records moved down (MpHeroRecord::Assign), the last
    // destroyed; the selection kept in the list, the list's scroll range (count - 10, else 0 and
    // the top row 0), the list drawn and the hero shown. Nothing without a selection.
    void DeleteHero();
    // 00615940 (slot 27, RET 4). A message box answered: 0x10001 (the "lmp_deletepers" question's
    // OK) deletes the hero; then the selected hero's record takes the local player's party and
    // sends the transfer (00662AC0); mode 9 (00615D30).
    void OnMessageResult(int result);
    // 00617A50 (ui_hero_screen_click.cpp). Region 1 / Enter of the statistics page: the new hero
    // keeps only the chosen weapon, quick item and spell container (the others removed from its
    // lists; the shown weapon 0, or -1 without one), and a spell container whose spell the hero
    // cannot use (HeroCanUseSpell) goes back to the local player's items; a new hero record is
    // appended (the vector's own growth through 00619920 when full) and selected, its file named
    // "<the highest number among the folder's *.mp titles + 1>.mp", given the local party and
    // saved (00662AC0); the list's scroll range, the list drawn, mode 9.
    void AddNewHero();
    // 00615D30 (RET 4; Rebirth-patched; ui_hero_screen_mode.cpp, reproducing WideScreen, so hooked
    // in both launches; rewritten callers go through their forwarder). The screen
    // laid out for `mode`. First everything off: the menu model's parts, the nine child windows,
    // every sprite and surface, the regions after the ninth removed and the others' tips emptied.
    // 9, the hero list: the model's "but03" and "button03_multiplayer", the list's scroll bar, all
    // sprites but 7-9, the four frame quarters (sprites 3-6), the buttons' regions and tips,
    // surfaces 0 and 1, the name edit off, the selection frame (sprite 2), ten list rows as
    // regions and the selected hero shown. 10, the new hero: the voices' scroll bar, sprites 3-9,
    // the quarters, the bars at 300, the statistics panel and both figure strips, the regions
    // and tips, the name edit on and editing, surface 2, the selection frame, eleven voice rows
    // and the voices drawn. 11, its statistics: sprites 7-9, the bars at 400, the equipment,
    // spell, trade and statistics panels, two regions and three tips, surface 2, the name edit
    // off, the trade panel's hero 0. Last, for every mode: the description panel emptied, the
    // statistics panel's page `mode`, mode_. With Rebirth's WideScreen the curtains (sprites 19 and
    // 20, when there are 21) are then shown in modes 10 and 11 and hidden in 9.
    void SetModeVanilla(int mode);
    // 00613EA0 (slot 25; Rebirth-patched; ui_hero_screen_create.cpp, reproducing WideScreen; hooked
    // in the vanilla launch only, as EI_Plugin.asi patches it too in a Rebirth launch). The game's resources taken for a network game
    // (0057DEE0 with 1); the menu model with its multiplayer button; nine regions; the item
    // screen's base (005EDD80); the sprites (0, 1 the buttons, 2 the selection frame, 3-6 the
    // quarters "campm1".."campm4" around the figure, 7-9 the new hero's buttons, 10-12 frames,
    // 13, 14 backgrounds, 15-18 the commands' buttons); the description, statistics, equipment,
    // quick item and trade panels; the list's scroll bar; the figures a new hero can take: every
    // creature record (007AF378) whose unit prototype's figure model is "unhuma" (defaults_,
    // voiceLists_[0]) or "unhufe" (names_, voiceLists_[1]), a voice "<lmp_male_voice /
    // lmp_female_voice> <n>" only when the record's +0x5C is set; both name lists sorted, the
    // women's then reversed; the two figure strips; the rename and name edit windows; the voices'
    // scroll bar; four surfaces (the list, its commands with the four "lmp_pers_..." texts, the
    // voices, the name being changed, hidden); the saved heroes reloaded from <data>"mp\"
    // (00662FB0), the list's scroll range, the list drawn, the first hero selected (-1 without
    // one) and mode 9 (00615D30); with Rebirth's WideScreen then the two curtains over the
    // screen's margins, hidden (widescreen::AddCurtains; sprites 19 and 20).
    void Create();
    // The hero list's scroll bar after the count changed (DeleteHero and AddNewHero inline it):
    // range count - 10 keeping the position, or 0 with the top row 0.
    void RefreshHeroScroll();

    CUiWindow window;
    std::uint8_t unused_148[0x164 - 0x148];
    CItemInfoPanel* descPanel_; // +0x164 the description panel
    std::uint8_t* hoveredItem_; // +0x168 the item under the mouse (set by Draw)
    CItemIconWindow* iconWindow_;   // +0x16C the item screens' icon window (its two bars, PlaceBars)
    ItemObjectCache models_;    // +0x170 the item screens' model cache (00633F50 constructs it)
    ItemObjectCache textures_;  // +0x180 and the texture cache (00633FF0)
    int mode_;                  // +0x190 9, 10, 11
    int heroIndex_;             // +0x194 selected hero
    void* heroScroll_;          // +0x198 scroll bar of the hero list
    int heroTop_;               // +0x19C first row shown
    std::uint8_t* previewModel_;// +0x1A0 the hero's figure shown by the preview (005EC300)
    char* weapon_;              // +0x1A4
    char* armor_;               // +0x1A8
    char* spellContainer_;      // +0x1AC
    void* statsPanel_;          // +0x1B0 (CHeroStatsPanel; 006232D0 sets its hero)
    void* weaponPanel_;         // +0x1B4 shows the weapon and the armour
    void* spellPanel_;          // +0x1B8
    CUiItemStrip* figureStrips_[2]; // +0x1BC the figures of defaults_ (voices voiceLists_[0]), +0x1C0 of names_ (voiceLists_[1])
    void* tradePanel_;          // +0x1C4 (CTradePanel; 006255A0)
    void** defaults_;           // +0x1C8 STLport vector<CString>; its first entry goes to slot 52 for a new hero
    void** defaultsEnd_;        // +0x1CC
    void** defaultsCapacity_;   // +0x1D0 (also its allocator proxy)
    MpStringVector names_;      // +0x1D4 STLport vector<CString>
    float turn_;                // +0x1E0 the preview's turn
    int rotating_;              // +0x1E4 0, 1 or 2 while a rotate arrow is held
    CUiEditField* renameEdit_;  // +0x1E8 the hero list's rename edit window (focused_ while renaming)
    CUiEditField* nameEdit_;    // +0x1EC the new hero's name edit window (0065C290 sets its text)
    void* voiceScroll_;         // +0x1F0
    const char* text_;          // +0x1F4 CString
    MpVoiceVector voiceLists_[2];   // +0x1F8, +0x204 (voices_ points at one of them)
    char** voices_;             // +0x210 STLport vector of {CString file, CString name} (8 bytes)
    int voiceTop_;              // +0x214
    int voiceIndex_;            // +0x218
    int voiceSound_;            // +0x21C
};
static_assert(offsetof(CMpHeroScreen, rotating_) == 0x1E4);
static_assert(offsetof(CMpHeroScreen, descPanel_) == 0x164);
static_assert(offsetof(CMpHeroScreen, previewModel_) == 0x1A0);
static_assert(offsetof(CMpHeroScreen, renameEdit_) == 0x1E8);
static_assert(offsetof(CMpHeroScreen, tradePanel_) == 0x1C4);
static_assert(offsetof(CMpHeroScreen, mode_) == 0x190);
static_assert(offsetof(CMpHeroScreen, heroTop_) == 0x19C);
static_assert(offsetof(CMpHeroScreen, weapon_) == 0x1A4);
static_assert(offsetof(CMpHeroScreen, weaponPanel_) == 0x1B4);
static_assert(offsetof(CMpHeroScreen, voiceScroll_) == 0x1F0);
static_assert(offsetof(CMpHeroScreen, voices_) == 0x210);
static_assert(offsetof(CMpHeroScreen, names_) == 0x1D4);
static_assert(offsetof(CMpHeroScreen, voiceLists_) == 0x1F8);
static_assert(offsetof(CMpHeroScreen, models_) == 0x170 && offsetof(CMpHeroScreen, textures_) == 0x180);
static_assert(offsetof(CMpHeroScreen, defaults_) == 0x1C8 && offsetof(CMpHeroScreen, text_) == 0x1F4);
static_assert(sizeof(CMpHeroScreen) == 0x220);

// An entry of a sound set (0x2C bytes; the sets are the values of the map 007C8A6C, keyed by a
// unit prototype's name; the set's entries are an STLport vector at +0x1AC).
struct UnitSoundEntry {
    const char* name;              // +0x00
    const char* sound;             // +0x04 what the interface plays
    std::uint8_t unused_08[0x28 - 0x08];
    std::uint32_t flags;           // +0x28 (the hero screen's voice takes entries with 0x2282 set)
};
static_assert(sizeof(UnitSoundEntry) == 0x2C);
// A sound set (the map's value).
struct UnitSoundBank {
    std::uint8_t unused_000[0x1AC];
    stlp::vector<UnitSoundEntry> entries;   // +0x1AC
};
static_assert(offsetof(UnitSoundBank, entries) == 0x1AC);

// --- Multiplayer connection screen (0061B2C0-0061E8F0; ui_connect_screen.cpp) ----------------
//
// The servers found (config\addrbook.ini keeps the ones asked directly): a list of 15 rows with a
// scroll bar, row 0 the entry that hosts a new game (its name, player count slider, game type
// tabs, password and "unlisted" switch). Regions (Create 0061B2C0, Rebirth-patched): 0 connect /
// host, 1 back, 2 ask the master server, 3 add / forget an address, 4 the unlisted switch, 5 the
// password, 6-9 the sort columns, 10-13 the game type tabs, 14 and up the list rows. Sprites
// (scene objects): 4 the unlisted switch, 5 the password's, 6-9 the game type tabs (uOffset_ 0.09
// selected). The list is drawn by 0061D780 (Rebirth-patched, called by address). Multiplayer only.

// An entry of the connection screen's list (0x34 bytes; a vector<MpAddressEntry> at +0x168).
struct MpAddressEntry {
    // 0061EE80 (RET 4). Copy constructor; returns this.
    MpAddressEntry* CopyConstruct(const MpAddressEntry* other);
    // 0061E8F0 (RET 4). Assignment (every field; the padding is not copied); returns this.
    MpAddressEntry* AssignFrom(const MpAddressEntry& other);
    // 0061E8A0. Destructor: the description, then the name.
    void Destroy();

    mfc::CString name;             // +0x00 the server's name
    mfc::CString description;      // +0x04 the current map's title
    int players;                   // +0x08
    int maxPlayers;                // +0x0C
    int ping;                      // +0x10 milliseconds (9999: none)
    net::NetAddress address;       // +0x14
    int gameType;                  // +0x24 0..3 (4: the new game entry)
    bool foreign;                  // +0x28 the server answered with another protocol
    int state;                     // +0x2C net::ServerEntryBase::State (1: asked directly, forgettable)
    bool password;                 // +0x30 joining needs a password
};
static_assert(offsetof(MpAddressEntry, address) == 0x14);
static_assert(offsetof(MpAddressEntry, gameType) == 0x24);
static_assert(offsetof(MpAddressEntry, state) == 0x2C);
static_assert(offsetof(MpAddressEntry, password) == 0x30);
static_assert(sizeof(MpAddressEntry) == 0x34);
using MpAddressList = stlp::vector<MpAddressEntry>;

// The vector's own instantiation members (ui_connect_screen.cpp; thiscall on the vector).
// 0061E5A0: a default entry (empty texts, address 0, the rest left) appended. 0061E660 (RET 4):
// one inserted before `pos` (the tail moved up by assignment); returns it. 0061EA90 (RET 8):
// [first, last) erased (the tail assigned down, the rest destroyed); returns `first`.
void __fastcall MpAddressesPushDefault(MpAddressList* list);
MpAddressEntry* __fastcall MpAddressesInsertDefault(MpAddressList* list, void*, MpAddressEntry* pos);
MpAddressEntry* __fastcall MpAddressesErase(MpAddressList* list, void*, MpAddressEntry* first, MpAddressEntry* last);

class CMpConnectScreen : public CUiContainer {
public:
    static void** Vtable() { return reinterpret_cast<void**>(std::uintptr_t(0x00741CD0)); }
    static void** InputVtable() { return reinterpret_cast<void**>(std::uintptr_t(0x00741C9C)); }

    // The message box results (slot 27): the kinds the screen opens boxes with, ORed with 1 (OK)
    // or 2 (cancel) when they come back.
    enum BoxKind : std::uint32_t {
        kBoxNotice = 0x10000,         // a message (refusal, wrong protocol)
        kBoxAddress = 0x20000,        // an address to ask
        kBoxPassword = 0x30000,       // the new game's password
        kBoxMaster = 0x40000,         // the master server request (CMasterRequestBox)
        kBoxJoinPassword = 0x50000,   // the password of the server joined
    };
    static constexpr int kRows = 15;

    // 00618570 (ui_window_delete.cpp). The destructor body: the two text arrays, the password,
    // the entries (texts, storage), then the container's.
    void DestroyBody();
    // 00618550 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CMpConnectScreen* ConnectScreenDelete(unsigned flags);
    // 0061B2C0 (slot 25; Rebirth-patched, this is the vanilla body, hooked in the vanilla launch
    // only). The screen over 800 x 600: the "saveload" and "scrollbar" textures, the stone menu
    // model ("unmoco2" with "MainMenu00labels", parts "but03" and "button03_multiplayer") at
    // (400, 350, 8) turned about x by 007C7F40 * 1.2; the regions (above) with tips 0x9E98,
    // 0x9E99, 0x9EA2 and 0x9EA3 on the first four; sprites 0 / 1 the two buttons, 2 the selection
    // bar (plain 0xC8A06800), 3 the sort arrow (the scroll bar's texture, hidden), 4 / 5 the
    // unlisted and password switches, 6-9 the game type tabs (their names "lmp_allod_name_%d"
    // into allodNames_, the first selected), 10 / 11 two bars, three frames and three dark
    // backgrounds; fifteen row regions; surface 0 (100, 100)-(700, 500); the scroll bar, the
    // player slider (position 5, maxPlayers_ 6) and the name edit (the first hero's name to its
    // '|', at most 10 characters, trimmed; not focused) as children; the 22 "lmp_connection_%d"
    // texts; the client started (0057EA50); the queries read from config\addrbook.ini (a chunk
    // archive, chunk 1; an extra word of 0 becomes a random one xor the tick count) and given to
    // the client (00442300) when the file opens; nothing selected, the list rebuilt and drawn,
    // the help topic (005F86F0 with 0), and surface 1 with text 8 ("connecting").
    void Create();
    // 0061C430 (slot 24). Surface 1 ("connecting") shown while the client asks a host (join
    // state 1); when it stops asking, a refusal box ("lmp_server_full",
    // "..._you_are_banned", "..._wrong_password", else "..._cannot_connect"; text key + "_msg")
    // and nothing else this frame. Otherwise the children, and every 0.5 s the client asks the
    // selected address on each refresh (none for row 0), refreshes, and the list is rebuilt and
    // drawn; then the scroll bar, the player slider (maxPlayers_ = its position + 1) and a changed
    // name redraw the list.
    void Update();
    // Slot 26 (0061C2D0) does nothing and slot 41 (0061C420) returns 0: too short to hook.
    // 0061C2E0 (slot 27, RET 4). A box answered: an address (0x20001) is parsed (port 8888 unless
    // given) and asked when known, the address book written, the list drawn; the master request
    // ending (0x40002) draws the list; the new game's password (0x30001 / 0x30002) is kept and its
    // switch shows whether there is one; a join password (0x50001) joins the selected server with
    // it and sets the client's game type from the row.
    void OnBoxResult(std::uint32_t result);
    // 0061C700 (input slot 2, RET 0xC). By region (above): 0 Connect, 1 back (the cancel sound,
    // the address book written, the network sessions dropped, the screen closed with 0x1000001), 2
    // MasterRequest, 3 EditAddress, 4 the unlisted switch, 5 the password (none: a box for one;
    // else it is dropped), 6-9 the sort column (a second click reverses it; the list rebuilt), 10-13
    // the game type (only with row 0 selected; not drawn again), 14 and up a row (the select
    // sound, it and its address selected); then the container's handler.
    void OnLButtonDown(int buttons, int x, int y);
    // 0061CA30 (input slot 7, RET 0xC). A row double-clicked is selected (no sound, no drawing) and
    // Connect follows; then the container's handler.
    void OnLButtonDouble(int buttons, int x, int y);
    // 0061CAD0 (input slot 9, RET 0xC). The container's handler first; then Enter connects, Esc
    // leaves (as region 1 without the address book), Up / Down / PgUp / PgDn move the selection by
    // 1 / 15 with the select sound (SelectAndScroll); those keys are marked used.
    void OnKeyDown(int key, int repeat, int flags);
    // 0061CC00. The selection clamped to the list (an empty list gives -1 and reads the entry
    // before it, as the original), its address kept, the list scrolled to show it (the bar's
    // slot 25, then its position read back) and drawn.
    void SelectAndScroll();
    // 0061CCE0. The OK sound; row 0 hosts (GameFlow::StartServer: the name typed or the default
    // 00783D6C, the player slider + 1, gameType_, hostPassword_, unlisted_, the client's queries'
    // extra word) after the address book is written; another row joins: a foreign server gets the
    // "lmp_wrong_protocol" box; otherwise the address book is written, a password server opens
    // the password box, the server is joined at once with no password (0057E970) and the
    // client's game type set from the row.
    void Connect();
    // 0061CF50. On a valid selection: an entry asked directly is forgotten (and the address book
    // written); another opens the address box (texts_[19]) with the row's address (empty for row 0).
    void EditAddress();
    // 0061D0D0. The master server request: the master address (the text at 0079BED8, port 28004)
    // in a CMasterRequestBox (texts_[20]) with the client's queries' extra word.
    void MasterRequest();
    // 0061D1C0. Writes the client's queries to config\addrbook.ini (a chunk archive, chunk 1).
    void SaveAddressBook();
    // 0061D780 (Rebirth-patched: WideScreen (W2) and ExtendedServerName (W2a) reproduced behind their
    // flags; ui_connect_list.cpp; the screen's other members call it through its address). The list
    // drawn on surface 0. With row 0 (the new game) selected its controls show: the player slider
    // and the name edit enabled (and editing), sprites 4 and 5 and their regions' tips (0x9EA4 /
    // 0x9EA5, set once: newGameTips_); otherwise they are off and the tips empty. Texts 10 and 11
    // by the two buttons, the column titles (texts 12-15), the sort arrow (sprite 3: turned for an
    // odd key, at its column while a key is set) and a line under the titles (a pen of the text
    // colour); 15 rows from scrollTop_ in the colour of the entry's state (00793AD8: 1 0x31B3FF,
    // 2 0x31E3EE, else 0xA7D7E4): the new game row "<text 7> <the name typed>", the others name,
    // players "n/m", the game type's allod name ("?" for 4) and the ping; the selection frame
    // (sprite 2) on the selected row. Then the selected entry's details at the right in 0xA7D7E4:
    // for the new game the game type tabs (sprites 6-9) with their names, texts 0, 2 (with
    // maxPlayers_), 3, 16 and 17; for a server its name, players, game type, the map's title
    // wrapped over up to three lines, address, ping and, with a password, text 21.
    void DrawListVanilla();
    // 0061D470. The list again: the client's servers (ServerAt) in order, sorted by sortKey_
    // (std::sort, 0061EF60 / 0061F6F0 / 0061FB00), the new game entry inserted first; the
    // selection follows its address (row 0 and address 0 when gone); the scroll bar's range is the
    // rows past 15 and its position set again; scrollTop_ 0.
    void RebuildList();

    float refreshTimer_;           // +0x164 seconds since the last refresh (0.5 s period)
    MpAddressList entries_;        // +0x168
    int selected_;                 // +0x174 row
    net::NetAddress selectedAddress_;   // +0x178 the selected row's address (0: row 0)
    int scrollTop_;                // +0x188 the first row shown
    CUiScrollbar* scroll_;         // +0x18C
    CUiSlider* playersSlider_;     // +0x190 the new game's maximum players - 1
    CUiEditField* nameEdit_;       // +0x194 the new game's name
    int maxPlayers_;               // +0x198 the slider's position + 1 (6 from Create)
    int gameType_;                 // +0x19C the new game's type tab 0..3
    char boxText_[0x80];           // +0x1A0 the edit boxes' text (an address, a password)
    bool unlisted_;                // +0x220 the new game is not announced (1 from Create)
    std::uint8_t unused_221[3];
    mfc::CString hostPassword_;    // +0x224 the new game's password
    mfc::CStringArray allodNames_; // +0x228 "lmp_allod_name %d" texts (the game type tabs)
    bool joining_;                 // +0x23C the client was asking a host last frame
    std::uint8_t unused_23D[3];
    mfc::CStringArray texts_;      // +0x240 "lmp_connection %d" texts (22)
    int spare_;                    // +0x254 0 from Create; nothing else touches it
    int sortKey_;                  // +0x258 0 none; 1..8 the column pairs (odd ascending)
    bool newGameTips_;             // +0x25C the unlisted and password regions have their tips (DrawList; 0 from Create)
    std::uint8_t unused_25D[3];
};
static_assert(offsetof(CMpConnectScreen, refreshTimer_) == 0x164);
static_assert(offsetof(CMpConnectScreen, selected_) == 0x174);
static_assert(offsetof(CMpConnectScreen, selectedAddress_) == 0x178);
static_assert(offsetof(CMpConnectScreen, scroll_) == 0x18C);
static_assert(offsetof(CMpConnectScreen, gameType_) == 0x19C);
static_assert(offsetof(CMpConnectScreen, boxText_) == 0x1A0);
static_assert(offsetof(CMpConnectScreen, unlisted_) == 0x220);
static_assert(offsetof(CMpConnectScreen, hostPassword_) == 0x224);
static_assert(offsetof(CMpConnectScreen, joining_) == 0x23C);
static_assert(offsetof(CMpConnectScreen, texts_) == 0x240);
static_assert(offsetof(CMpConnectScreen, sortKey_) == 0x258);
static_assert(sizeof(CMpConnectScreen) == 0x260);

// A message box waiting on a master server request (0x1C4 bytes, vtables 00741DD8 / 00741DA4;
// opened by the connection screen's MasterRequest): the box with one cancel button, the link to
// the master server and a progress bar sprite. Its result is kBoxMaster (| 2 when done).
class CMasterRequestBox : public CMessageBox {
public:
    static void** Vtable() { return reinterpret_cast<void**>(std::uintptr_t(0x00741DD8)); }
    static void** InputVtable() { return reinterpret_cast<void**>(std::uintptr_t(0x00741DA4)); }
    // 0061E2F0 (RET 0x18). Constructor: the box (flags 2: one cancel button; format 1), the link
    // connecting to `address`, vtables, the bar (a plain sprite, colour 0xFFA06800), and `extra`
    // written to the link's outgoing buffer. Returns this.
    CMasterRequestBox* Construct(CInterface3D* ui, const char* titleKey, const char* textKey, std::uint32_t result,
                                 const net::NetAddress* address, std::uint32_t extra);
    // 0061E460 (ui_window_delete.cpp). The destructor body: the link (00440D40), then the box's.
    void DestroyBody();
    // 0061E440 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CMasterRequestBox* MasterRequestBoxDelete(unsigned flags);
    // 0061E4E0 (slot 24). The bar's right edge at 225 + 355 * the received share (0041D9A0); the
    // link steps: done (3) hands the reply to the client's server list (0041D600); the box closes
    // (0x40002) when the link is done or, stepping again, closed (up to three steps a frame).
    void Update();

    net::MasterLink link_;           // +0x180
    CUiMesh* bar_;                   // +0x1C0
};
static_assert(offsetof(CMasterRequestBox, link_) == 0x180);
static_assert(offsetof(CMasterRequestBox, bar_) == 0x1C0);
static_assert(sizeof(CMasterRequestBox) == 0x1C4);

// The multiplayer heroes (records of 0x74 bytes; +0x30 CString name, +0x70 CString file).
inline const RecordTable& MpHeroes() { return *reinterpret_cast<const RecordTable*>(0x007C82A4); }
constexpr int kMpHeroSize = 0x74;
// A multiplayer hero record: its party (CParty, 0x70 bytes) and file name.
struct MpHeroRecord {
    // 0061A350 (RET 4; ui_hero_screen_click.cpp). Assignment: the party (00661300), the file
    // name. Returns this.
    MpHeroRecord* Assign(const MpHeroRecord* other);
    // 00619540. Constructor: the party (00661050), an empty file name. Returns this.
    MpHeroRecord* Construct();
    // 00619560. Destructor: the file name, then the party (006611D0).
    void Destruct();
    // 00664AE0 (ui_hero_screen_click.cpp). Scalar deleting destructor (the vector's element
    // destructor with flag 0). Returns this.
    MpHeroRecord* DeletingDestruct(unsigned flags);
    // 00664410 (ui_hero_screen_click.cpp; fastcall, the destination on the stack, RET 4). copy():
    // [first, last) assigned (Assign) to dest onwards; returns the end of the destination.
    static MpHeroRecord* __fastcall CopyRange(const MpHeroRecord* first, const MpHeroRecord* last, MpHeroRecord* dest);
    // 00662AC0 (RET 4; party_file.cpp). Writes the party to `folder` + file: a file name with no
    // '.' or with anything but digits before the last one is deleted instead. Otherwise the old
    // file is moved to `folder` + "temp.mp", the party saved (with its variables) into a memory
    // stream and that written as a save file (magic 0x114, the save version), then the temporary
    // file deleted.
    void WriteFile(const char* folder);
    // 00662DD0 (RET 4, AL; party_file.cpp). Reads the party from `folder` + file (magic 0x114,
    // the versions 00790260..00790268): false when the file does not read, else the party loaded
    // (with its variables) and the memory stream's ok flag.
    bool ReadFile(const char* folder);
    CParty* Party() { return reinterpret_cast<CParty*>(party); }

    std::uint8_t party[0x70];        // +0x00 CParty
    mfc::CString file;               // +0x70
};
static_assert(sizeof(MpHeroRecord) == kMpHeroSize);

// The multiplayer heroes (007C82A0; constructed by the static initialiser 0065F580): their folder,
// the records (MpHeroes() above) and the current hero.
struct MpHeroRoster {
    // 00662F80 (RET 4; ui_hero_screen_click.cpp, A-H25). `hero` becomes the current one
    // (MpHeroRecord's assignment, inlined in the original).
    void Select(const MpHeroRecord* hero);

    mfc::CString folder;                    // +0x00
    stlp::vector<MpHeroRecord> records;     // +0x04
    MpHeroRecord current;                   // +0x10
};
static_assert(offsetof(MpHeroRoster, records) == 0x04 && offsetof(MpHeroRoster, current) == 0x10 &&
              sizeof(MpHeroRoster) == 0x84);

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
    // 0062ACB0 (slot 26; ui_small_members.cpp). The menu closes: its stone model (models_[0])
    // plays animation 0.
    void OnClose();

    CUiWindow window;
    std::uint8_t unused_148[0x164 - 0x148];
    int hovered_;               // +0x164 region under the mouse, -1 for none
};
static_assert(offsetof(CEscMenu, hovered_) == 0x164);

// --- Credits ------------------------------------------------------------------------------------
//
// The titles window (vtables 00740C08/00740C3C, 0x1FC bytes; main menu region 4).
// A line of the titles' script (the value of a lines_ node, after next / prev).
struct TitleLine {
    // 0062BCB0 (ui_credits.cpp). The destructor: style, then text.
    void Destroy();

    bool scroll;                // +0 the first word is "#scroll"
    std::uint8_t unused_1[3];
    float speed;                // +4 the second word (atof): the scroll speed, or the seconds shown
    const char* text;           // +8 CString: the fourth word (the header)
    const char* style;          // +0xC CString: the third word (the text file in the archive)
};
static_assert(sizeof(TitleLine) == 0x10);

// The titles' scrolling text surface (0x58 bytes, vtable 00742FD8: slot 0 Draw, slot 1 the deleting
// destructor; ui_scrolling_surface.cpp).
class CUiScrollingSurface : public CUiSurface {
public:
    // 0065ED30 (RET 8). A surface of format 5 over the layout rectangle; "Riched20.dll" loaded and
    // a hidden multi-line "RichEdit20A" child of the game window created; white background, the
    // offsets and the speed 0 (startTime_ and textLength_ are left as they were). Returns this.
    CUiScrollingSurface* Construct(CInterface3D* ui, const RECT* rect);
    // 0065EDE0. Destructor body: the control destroyed, the library freed, the surface released.
    void DestructBody();
    // 0065EDC0 (slot 1, RET 4).
    CUiScrollingSurface* DeletingDestruct(unsigned deleteFlags);
    // 0065EF30 (RET 8). Loads the text file `name` of `archive` into the surface (LoadRtf) and
    // closes it; whether it was found.
    bool Load(CMappedResource* archive, const char* name);
    // 0065EF80 (RET 8). Streams the RTF block into the control (EM_STREAMIN; the character count
    // into textLength_), the offset 0, the text rendered. Returns true.
    bool LoadRtf(const void* data, std::uint32_t size);
    // 0065F170 (slot 0). While the speed is not 0 the offset moves by the game time passed times
    // the speed (truncated) and the text is rendered again; then the surface's own draw (005FDA80).
    void Draw();
    // 0065F1C0 (RET 8). Starts the text at `speed` (0: still), from below the surface when
    // `fromBelow` (the offset minus the surface's height in twips; the DC taken and released for
    // its resolution); the end offset 0, the start time the game time.
    void Start(float speed, char fromBelow);
    // Load and Start through their addresses (the checks run): ui_credits.cpp.
    bool LoadThroughHook(void* archive, const char* name);
    void StartThroughHook(float speed, char fromBelow);
    // 0065F020 (ui_scrolling_surface.cpp, A-H17). Renders the RichEdit's text into the surface:
    // the DC held, the surface cleared, the background colour (+0x34) set (EM_SETBKGNDCOLOR), the
    // whole text formatted (EM_FORMATRANGE) into the surface's size in twips shifted by the scroll
    // offsets (+0x40, +0x44; 1000 twips more below). The first time the text fits (the returned
    // character index reaches +0x4C) the offset is kept in +0x50; once the offset has moved 1000
    // twips plus the height past it, the speed and +0x50 become 0. The DC is released.
    void RenderText();

    HWND richEdit_;             // +0x38 the RichEdit control holding the text
    float startTime_;           // +0x3C
    int offsetX_;               // +0x40 twips
    int offset_;                // +0x44 twips scrolled
    float speed_;               // +0x48 0 once the text has scrolled through
    int textLength_;            // +0x4C characters (set when the text is loaded)
    int endOffset_;             // +0x50 offset_ when the end of the text came into view (0: not yet)
    HMODULE richEditLibrary_;   // +0x54 "Riched20.dll" (null when it did not load)

    static constexpr std::uintptr_t kScrollingVtableAddress = std::uintptr_t(0x00742FD8);
};
static_assert(offsetof(CUiScrollingSurface, richEdit_) == 0x38);
static_assert(offsetof(CUiScrollingSurface, endOffset_) == 0x50);
static_assert(offsetof(CUiScrollingSurface, speed_) == 0x48);
static_assert(sizeof(CUiScrollingSurface) == 0x58);

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
    // 0062BD80 (slot 20, RET 4; ui_credits.cpp). Once the game time passes nextLineAt_ and the
    // text has stopped: both surfaces frozen, then the next line: its text file loaded and started
    // (a "#scroll" line scrolls at its speed under its header, another stays for its speed in
    // seconds), or past the last line finished_. The movie steps (005DBD80, stretched unless the
    // movie flag 00784752 is 1); when it has ended after the last line: "<name>fout" played, the
    // screen flag cleared and the titles closed as Close() does (returns 1); otherwise the
    // container's draw.
    BOOL Draw(int pass);
    // 0062BD00 (slot 26; ui_credits.cpp). The lines freed, font smoothing set back to
    // fontSmoothing_ (never read: kept), the display's +0x14 restored, the screen flag set and the
    // cursor shown.
    void OnClose();
    // 0062BFD0 (slot 27, RET 4; ui_credits.cpp). A window above closed: the movie player is
    // closed (005DBF70).
    void OnMessageResult(int result);

    CUiWindow window;
    std::uint8_t unused_148[0x164 - 0x148];
    CUiScrollingSurface* textSurface_;   // +0x164 the scrolling text
    CUiSurface* headerSurface_; // +0x168
    int fontSmoothing_;         // +0x16C passed (as uiParam) to SPI_GETFONTSMOOTHING, never written
    std::uint8_t archive_[0x1C8 - 0x170];   // +0x170 CMappedResource ("outro.res")
    stlp::list<TitleLine> lines_;   // +0x1C8
    float nextLineAt_;          // +0x1CC game time the current line ends (0 from Build and for scrolls)
    stlp::list<TitleLine>::Node* currentLine_;   // +0x1D0 the next line (the sentinel at the end)
    const char* name_;          // +0x1D4 CString ("Titles")
    CBinkVideo player_;         // +0x1D8 the movie player (constructor 005DB340)
    bool fromGame_;             // +0x1F4 opened in a game (pending value 0xCCCCCCCC) or the menu
    bool finished_;             // +0x1F5 the last line has gone (cleared by Build)
    std::uint8_t unused_1F6[2];
    int savedDisplay_;          // +0x1F8 the renderer's (00500960) fog colour while the titles run
};
static_assert(offsetof(CCreditsScreen, textSurface_) == 0x164);
static_assert(offsetof(CCreditsScreen, lines_) == 0x1C8);
static_assert(offsetof(CCreditsScreen, fromGame_) == 0x1F4);
static_assert(offsetof(CCreditsScreen, savedDisplay_) == 0x1F8);
static_assert(sizeof(CCreditsScreen) == 0x1FC);


// --- Main menu ----------------------------------------------------------------------------------
//
// The main menu window (input vtable slot 2 at 0074065C).
// The main menu (CreateScreen id 2, vtables 00740688 / 00740654, 0x17C bytes; ui_main_menu.cpp, the
// click in ui_layouts.cpp). Regions 0-5 from the menus.reg "MainMenu" rectangles (007C8048..); the
// stones are the object manager's object 2; the world behind it runs the "MainMenu.cam" camera path
// with rain at night.
class CMainMenu : public CUiContainer {
public:
    // Body of the slot at 00632A90 (registered by member pointer).
    int DrawImpl(int pass);
    // 00632D70 (input slot 2; ui_layouts.cpp). By region: 0 the difficulty question (a message
    // box, vtables 0074225C / 00742228), 1 load (the save/load window), 2 the exit movie and quit,
    // 3 options, 4 the titles (surfaces cleared, opened over the menu), 5 multiplayer (status
    // "reserved_04", the server flag, the hero screen); each window is built inline as the
    // original does. Then the container's handler.
    void OnLButtonDown(int buttons, int x, int y);
    // 006326B0 (slot 25). The interface's saved state cleared (005F51F0), the flow's party step 0,
    // an inert extra input side (vtable 00741154), the full-screen rectangle, the version line, the
    // six regions; the default music, the map edge fade kept (+0x178) and set to 6, rain by a coin
    // toss (+0x16C), daylight between 7 and 21 (+0x16D), the rain started before 7; the camera path
    // "MainMenu.cam" and the menu music.
    void Create();
    // 006335E0. A surface (100, 480)-(780, 600) with the "version_name" text, added to the window.
    void AddVersionLine();
    // 00632B10 (slot 24, every frame). The children's update; with rain the weather turns on at 0
    // o'clock and off at 7 (the truncated hour changing); at night before 6 with rain and the
    // option 007AF064 bit 0, every 100 ms a 1 in 41 chance of a lightning bolt at random; the hour
    // kept in +0x170.
    void Update();
    // 00632910 (slot 26). The map edge fade put back (+0x178), then 00578920(1) and 00578940.
    void Close();
    // 00632940 (slot 27, RET 4). A message box's answer (the flow's party step 0 and the server
    // flag cleared first): 0x10001 (the quit box's yes) the exit movie and quit, 0x20001 the intro
    // movie and the flow's state 3, 0xCCCCCCCC (the titles closed) the version line again.
    void OnMessageResult(int result);
    // 00633400 (input slot 4, RET 0xC). Hovering a region plays its stone's animation (regions 0-5:
    // 1, 3, 5, 7, 11, 9; off them the rest pose) with the stone sound, once per region; then the
    // container's handler.
    void OnMouseMove(int buttons, int x, int y);
    // 00633540 (input slot 9, RET 0xC). The container's handler, then Esc asks to quit (a message
    // box "quit" / "quit_msg", flags 0x10000).
    void OnKeyDown(int key, int repeat, int flags);

    int hovered_;                    // +0x164 the region under the mouse (-1 none)
    std::uint8_t unused_168[4];
    bool rain_;                      // +0x16C
    bool daylight_;                  // +0x16D the hour was between 7 and 21 when the menu opened
    std::uint8_t unused_16E[2];
    float hour_;                     // +0x170 the time of day at the last update
    std::uint8_t unused_174[4];
    float savedEdgeFade_;            // +0x178 the map edge fade before the menu
};
static_assert(offsetof(CMainMenu, hovered_) == 0x164);
static_assert(offsetof(CMainMenu, rain_) == 0x16C);
static_assert(offsetof(CMainMenu, hour_) == 0x170);
static_assert(sizeof(CMainMenu) == 0x17C);

// --- Exchange window ------------------------------------------------------------------------
//
// Trading between two players (screen 9: vtables 00740D18 / 00740CE4, 0x1B8 bytes, constructor
// 005F6F30; xch_* texts; Create 0062C250 is its virtual slot 25). Multiplayer only.
class CParty;
class CTradeGrid;
class CExchangeScreen : public CItemScreen {
public:
    // 0062C250 (slot 25; ui_exchange.cpp). Builds the window: texture "campinfo", regions 0 accept,
    // 1 take back, 2 leave (tips 0xA028, 0xA029, 0xA02B), their four button sprites (the third
    // hidden, the leave button shaded 0.5), the item screens' base (005EDD80) with the icon
    // window's bars at y 300, a scratch party (made, slot 2), our offer (the client's +0x2620,
    // cleared) and the partner's (+0x2690), then the children: the description panel at (600,
    // 100), the local party's item strip (forwarding, action 1, items), the players' figure strip
    // (top, no arrow tips), the trade grid over both offers (mode 5) and the money field (0..200 x
    // 0x15E..0x16E, empty), and four 200-wide surfaces.
    void Create();
    // 0062C9C0 (slot 20, RET 4; ui_exchange.cpp). The item (or help topic) under the mouse from
    // the strip, else the grid, goes to the description panel; the four surfaces get
    // "xch_current_money" with the local party's money and "xch_accept", "xch_money_offer", and
    // while the partner's offer pairs with ours (0041E220) "xch_money_remote" with its money and
    // "xch_remote_header" with "xch_remote_ok" / "_cancel"; the accept / take back buttons are
    // shaded by our offer's acceptance and the third button by the partner's; then the children.
    // Returns 1.
    BOOL Draw(int pass);
    void OnKeyDown(int key, int repeat, int flags);   // 0062D090
    // 0062D210 (slot 37, RET 0xC; ui_exchange.cpp; CInterface3D::OnExchangeDone). The exchange
    // went through: the local party's items are released (00661FE0), every item of `partyC`, then
    // of `partyB`, returned into it (ReturnAll), its money set to partyB's - partyA's + partyC's,
    // and the client goes back to the world (00578B20).
    void OnExchangeDone(CParty* partyB, CParty* partyA, CParty* partyC);
    // 0062C8F0 (slot 26). The screen closes: the description panel describes nothing and the
    // scratch party is deleted (its slot 0).
    void Close();
    // 0062C920 (slot 27, RET 4). A message box's answer: nothing.
    void OnMessageResult(int result);
    // 0062C930 (slot 24). The children's update; then, when the client's offer (+0x260C) is gone
    // or detached, our offer's items go back to the local party and the client returns to the
    // world; else a changed money field (its changed_ cleared) becomes our offer's money (atoi)
    // and the offer is published (0041DA50).
    void Update();
    // 0062CF70 (input slot 2, RET 0xC). The container's handler, then by region: 0 accept (the buy
    // sound and the trade recorded with the local party, 0041E0A0, when our offer is live and not
    // accepted and its money is within the local party's; else the cancel sound), 1 take the
    // acceptance back (cancel sound and 0041E180, when it is accepted), 2 leave (the transit
    // sound and 0041E3E0). The event is not marked used.
    void OnLButtonDown(int buttons, int x, int y);
    // 0062D100 (slot 42, RET 8; ui_exchange.cpp; what the strip and the grid call with their mode).
    // Refused by slot 43: the cancel sound. Mode 1 (the strip): the put_on sound, `item` moves from
    // the local party into our offer (MoveItem with limit 8, merging) and the offer is published
    // (0041DA50). Mode 5 (the grid), only for an item of our offer: the put_off sound, the item
    // moves back to the local party, the strip scrolls to what the party now holds (00621570) and
    // the offer is published. Other modes nothing.
    void MoveOffered(CItem* item, int mode);
    // 0062D200 (slot 43, RET 8). Whether `item` may move with `mode`: always (AL 1).
    bool CanMove(CItem* item, int mode);

    CParty* scratch_;                // +0x190 a party of the screen's own (made by Create, deleted by Close)
    CParty* partnerOffer_;           // +0x194 the client's parties_[1]
    CParty* ownOffer_;               // +0x198 the client's parties_[0]
    CUiItemStrip* strip_;            // +0x19C the local party's items (forwarding strip, mode 1)
    CUiItemStrip* figures_;          // +0x1A0 the players (figure strip)
    CTradeGrid* grid_;               // +0x1A4 the two offers (mode 5)
    CUiEditField* moneyField_;       // +0x1A8 the money offered
    MpStringVector names_;           // +0x1AC CStrings: the figure strip's names
};
static_assert(offsetof(CExchangeScreen, scratch_) == 0x190);
static_assert(offsetof(CExchangeScreen, names_) == 0x1AC);
static_assert(sizeof(CExchangeScreen) == 0x1B8);
