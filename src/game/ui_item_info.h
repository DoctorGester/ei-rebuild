#pragma once

// The item description panel of the camp, the multiplayer hero screen and the exchange window
// (005EC000-005F3000; see docs/notes/ui.md, "Item descriptions"). Names provisional.
//
// The panel (vtables 007404DC / 007404A8, destructor body 005EE380, built by 005EE440) is a
// CUiWindow with one surface (200 x 300 at the layout point +0x1C0, +0x1C4) on which it writes
// what the mouse is over: an item (005EE6C0 puts it in the panel's item list and 005EE760
// describes it, dispatching on the item's kind), a statistic of the hero or the level-up page
// (005F1590) or a camp help topic (005F2910). Its slot 20 (005EE5C0) draws the described item's
// model through the owner screen (005ECA10, ui_item_view.cpp).
//
// Every description writes into the surface with the same layout: the title wrapped in white at
// (15, 10)-(185, 25), then lines 15 pixels apart in 0xA7D7E4, the description text wrapped last.

#include "item.h"
#include "ui.h"

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CItem;
class CItemInfoPanel;
namespace console {
class CStringArray;
}

// 005F13A0 (__fastcall, RET 4). The seven damage values against the last one: into `out` (emptied)
// the threshold ("%d" when `integer`, else "%.1f"), the damage kinds ("dmg_<n>" texts, ", "
// separated) of the first six above it and those below it (or unordered).
void __fastcall ItemDamageLists(const float* values, console::CStringArray* out, char integer);

// --- The item screens' model view (005ECA10; ui_item_view.cpp) ------------------------------------

// 005EC020 (__fastcall). A new 256 x 256 texture (texture manager 007AF744, format 0x20, counted)
// holding image "<name>.mmp" decoded and packed to 1:5:5:5 at the bottom left (rows of 512 bytes),
// or white when there is no such image; null when it cannot be made or locked.
void* __fastcall ItemFigureTexture(const char* name);
// 005EBFB0 (__fastcall; ui_item_model.cpp). The model manager's object `name`, or a 100 x 100
// interface sprite standing in for it; counted (one reference for the caller).
CEngineObject* __fastcall ItemModel(const char* name);

// An item drawn as a turning model in a cell (005ECA10's argument, 0x18 bytes).
struct ItemCell {
    CItem* item;     // +0x00
    float x;         // +0x04 the cell's centre (layout)
    float y;         // +0x08
    float depth;     // +0x0C
    float size;      // +0x10 the cell (100 in the strips and grids, 120 in the description panel)
    int mode;        // +0x14 the strip's action mode; 9: greyed out, never shaded as unusable
};
static_assert(sizeof(ItemCell) == 0x18);

// A hero's face drawn in a cell (005ED7B0's argument, 0x14 bytes; built by 00629AF0).
struct HeroFaceCell {
    const char* prototype;   // +0x00 the unit prototype's name (007AF364 map)
    float x;                 // +0x04 the cell's centre (layout)
    float y;                 // +0x08
    float depth;             // +0x0C
    float size;              // +0x10
};
static_assert(sizeof(HeroFaceCell) == 0x14);

// A cached object and when it was last used (timeGetTime).
struct ItemCacheEntry {
    void* object;
    std::uint32_t lastUse;
};

// A cache of the item screens (0x10 bytes at +0x170 and +0x180): an STLport map from a name to
// {object, time of last use} (0x24-byte nodes; lib_trees::ItemCache registers its members), and
// how many entries it keeps (30; 20 when 00784754 is set). 005F2F20 / 005F2F90 release the least
// recently used objects beyond that.
struct ItemObjectCache {
    using Map = stlp::map<stlp::string, ItemCacheEntry>;
    using Node = Map::Node;

    // The camp screen's constructor (005F6240, and the hero screen's copy of it): the map's header
    // from the pool (005FA420 / 005FA630, or 00619750 and 00619770), emptied (005FA400 / 005FA610)
    // with `compare` as its compare byte; the limit 50 (ui_item_screen.cpp).
    void Construct(std::uint8_t compare);

    Map map;
    std::uint32_t limit;   // +0x0C
};
static_assert(sizeof(ItemObjectCache) == 0x10 && sizeof(ItemObjectCache::Node) == 0x24);
// 00634500 / 00634560 (ui_hero_screen.cpp; thiscall on the cache). Every cached object released
// (slot 4), in key order (the hero screen's model and texture caches).
void __fastcall ReleaseCachedObjects(ItemObjectCache* cache);

// 005F2F20 / 005F2F90 (__fastcall on the cache). Release the least recently used entries of the
// model / texture cache beyond its limit (ui_item_screen.cpp).
void __fastcall TrimItemCache(ItemObjectCache* cache);

// An item's turning state (0x18 bytes, vtable 007403F8, a counted object at the item's +0x1C, its
// count a 16-bit word): made by the item draw (005ECA10) and the moves (005EE1B0).
struct ItemTurn {
    // 005ED710 (slot 3, ui_item_screen.cpp). A new state with the same angle, speed, flag and glow
    // and no references (null when the allocation fails, after writing the count through it).
    ItemTurn* Clone() const;
    // 005ED780 (slot 0, RET 4). The counted object's base vtable (005ED7A0), freed with bit 0 of
    // `flags`; returns this.
    ItemTurn* DeletingDestruct(unsigned flags);
    // 005ED760 (slot 2; ui_small_members.cpp). The object's vtables written again, the counted
    // root's (0073BB68) and then its own (007403F8): a construction in place.
    void ResetVtable();

    void* vtable;
    int references;          // +0x04
    float angle;             // +0x08 radians, wrapped at 007C7D20 (2 pi)
    float speed;             // +0x0C
    bool flag;               // +0x10 set by a move into a list shown turning
    std::uint8_t unused_11[3];
    float glow;              // +0x14 phase of the spell glow
};
static_assert(sizeof(ItemTurn) == 0x18);
// The item's turning state (CItem +0x1C).
inline ItemTurn*& ItemTurnOf(CItem* item) { return reinterpret_cast<ItemTurn*&>(item->link_); }

class CParty;
class CItemScreen;
// 005EC010 / 005EC2F0 (identical; ui_small_members.cpp). What the item screens' cache destructors
// (005F6470, 005F6620, 005F7010, 005F8FC0, 00634500, 00634560) call per cached model / texture:
// the object's slot 4 (its release), as a tail call; the slot's EAX is returned.
int __fastcall UiReleaseCachedModel(void* object);
int __fastcall UiReleaseCachedTexture(void* object);
// 005F6470 / 005F6620 (__fastcall on the screen). Destructor bodies of the item screens (vtable
// 00740A2C's screen; the camp, vtable 0074091C, which also has a CStringArray at +0x240 and a
// CArray at +0x19C): both caches' objects released and their maps destroyed, then the container
// base (ui_item_screen.cpp).
void __fastcall ItemScreenDestroy(CItemScreen* self);
void __fastcall CampScreenDestroy(CItemScreen* self);
// 005F7010 (__fastcall). Destructor body of screen 9 (vtable 00740D18): its vector of CStrings at
// +0x1AC, then both caches as above (ui_item_screen.cpp).
void __fastcall Screen9Destroy(CItemScreen* self);

// The item screens' icon window (0x148 bytes, vtables 00740444 / 00740410; made by 005EDD80).
class CItemIconWindow : public CUiWindow {
public:
    // 005F2A90 (slot 18). The frame sprites: a strip of 8 cells over the layout's middle, the
    // column edges and corners of four 200-wide columns, and six bars.
    void Build();
    // 005F2E00 (RET 4; the exchange window with 300). Places the two bars (sprites 25, 26) across
    // the left and right columns (0..100, 100..200) at y - 20..y + 20 (depth 7.8, scale 100).
    void PlaceBars(int y);
};

// The item screens (the camp, the multiplayer hero screen and the exchange window share these
// fields; 005EDD80 sets the caches up): the description panel, the item under the mouse, and the
// caches of item models (by the item's slot 20 name) and figure textures (slot 19 name).
class CItemScreen : public CUiWindow {
public:
    // 005F6FF0 (slot 0, the deleting destructor; ui_window_delete.cpp). Screen9Destroy, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CItemScreen* Screen9Delete(unsigned flags);
    // 005F6600 (slot 0, the deleting destructor; ui_window_delete.cpp). CampScreenDestroy, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CItemScreen* CampScreenDelete(unsigned flags);
    // 005F6450 (slot 0, the deleting destructor; ui_window_delete.cpp). ItemScreenDestroy, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CItemScreen* ItemScreenDelete(unsigned flags);
    // 005ECA10 (RET 8). Draws `cell`'s item: its model (cached, created by 005EBFB0) with its
    // texture (cached, 005EC020) turning while the mouse is on it, a glow in the colour of the
    // carried spell's school, a frame behind it, the modifier marks of a spell container, and a
    // shade over items the strip's mode cannot take (virtual slot 43, CanEquip).
    void DrawItem(int pass, ItemCell* cell);
    // 005ED7B0 (RET 8). Draws the face of `cell`'s prototype: model "infa<model>me<n + 1>face" (the
    // figure prototype's +0xE8 model name, the prototype's +0x10) with texture "face<m|f><+0xC,
    // two digits>" ("m" for model "unhuma", "f" for "unhufe", else nothing), both cached as the
    // items are, tilted back by (y - 300) / 1000 radians, over the cell frame.
    void DrawHeroFace(int pass, HeroFaceCell* cell);
    // The face's model and texture names (the lookups insert unknown names as DrawHeroFace's do).
    static void FaceNames(const char* prototype, mfc::CString* model, mfc::CString* texture);
    // 005EDD80. The screens' common Create: the icon window (+0x16C, CItemIconWindow, a child
    // with the screen's texture 0, built), textures "modifiers2" and "campslots", the display's
    // settings, and the caches' limits (30, or 20 with 00784754 set) applied (ui_item_screen.cpp).
    void CreateBase();
    // 005EE1B0 (RET 0x18; ui_item_screen.cpp). Moves `item` into `to` (CParty::AddItem with the
    // stack kept when `merge` and Shift (CInterface3D +0x7D) are down, merging with `merge`) and
    // out of `from` (RemoveItem, all of the stack likewise); returns what `to` now holds. With
    // `limit` > 0 and at least that many items in `to`, only when `item` is the same (slot 14) as
    // one of its first `limit` items (else nothing, null). The unconditional move with `turning`
    // gives the moved item a turning state (made if missing) with its flag set. Flags are bytes.
    CItem* MoveItem(CItem* item, CParty* to, CParty* from, int limit, int merge, int turning);
    // 005EE2C0 (RET 8; ui_item_screen.cpp). Moves every item of `from` into `to` (whole stacks,
    // merged); returns the last item `to` took, or null when `from` was empty.
    CItem* ReturnAll(CParty* to, CParty* from);
    // The plain item screen's answers (vtable 00740A2C; the exchange 00740D18 and the hero screen
    // 0074214C keep some of them; ui_small_members.cpp): 005F6380 (slot 43, RET 8) no item may
    // be moved (AL 0); 005F63A0 (slot 45, RET 0xC) no skill and no mana; 005F6400 (slot 50, RET 4)
    // no hero unit; 005F6430 (slot 53) no weight (0.0 from 0073B858, in ST0); 005F6440 (slot 54,
    // RET 8) no price.
    bool CanEquipDefault(CItem* item, int mode);
    void BestPartySkillDefault(int school, int* skill, int* mana);
    void* HeroUnitDefault(int index);
    float WeightDefault();
    int PriceDefault(CItem* item, int kind);

    std::uint8_t unused_148[0x164 - 0x148];
    CItemInfoPanel* descPanel_;      // +0x164
    CItem* hoveredItem_;             // +0x168 the item under the mouse (turns)
    CUiWindow* iconWindow_;          // +0x16C (005EDD80)
    ItemObjectCache models_;         // +0x170
    ItemObjectCache textures_;       // +0x180
};
static_assert(offsetof(CItemScreen, hoveredItem_) == 0x168);
static_assert(offsetof(CItemScreen, models_) == 0x170);
static_assert(offsetof(CItemScreen, textures_) == 0x180);

// What the statistics under the mouse are (0x18 bytes; filled by the stats panel 00624CB0 and the
// trade panel 00625690, read by DescribeStats).
struct ItemHoverInfo {
    int kind;                // +0x00 -1 none; 0 an attribute, 1 a skill, 2 a skill school, 3 a perk,
                             //       4 a body part's armour, 5 the weapon's attack or defence,
                             //       6 the magic skills, 7 a plain text
    int index;               // +0x04 the attribute, skill, school or perks learned
    int part;                // +0x08 body part (kind 4)
    CItem* weapon;           // +0x0C kind 5
    std::uint8_t* stats;     // +0x10 the hero's statistics (UnitStats, hero record + 0x20)
    mfc::CString text;       // +0x14 the text key
};
static_assert(sizeof(ItemHoverInfo) == 0x18);
static_assert(offsetof(ItemHoverInfo, stats) == 0x10);

class CItemInfoPanel : public CUiItemPanel {
public:
    // 005EE760. Clears the surface and describes the item in the list (nothing without one):
    // its own description for blueprints (005F0370), materials (005EFD90), spells (005F0B90),
    // modifiers (005F1130), quick items of category 8 (005EF300) and spells carried by an item
    // while the modifier key +0x7C is held or spell containers (005EF5D0); else the common
    // description (name, kind, material, price, condition, damage, armour, carried spell).
    void DescribeItem();
    void DescribeMaterial();        // 005EFD90
    void DescribeBlueprint();       // 005F0370
    void DescribeSpellRecord();     // 005F0B90
    void DescribeModifierRecord();  // 005F1130
    void DescribeQuickItem();       // 005EF300
    void DescribeCarriedSpell();    // 005EF5D0
    // 005F1590. Empties the list, clears the surface (DescribeItem) and, for a kind 0-7, writes the
    // statistic's title, values and description.
    void DescribeStats(ItemHoverInfo* info);
    // 005F2910. Empties the list, clears the surface and writes help topic "camphelp <topic>".
    void ShowHelp(int topic);
    // 005EE6C0. Describes `item` (null: nothing) unless it is already the described one (the
    // described item's slot 14).
    void Describe(CItem* item);
    // Input slots 9 / 11 (005EE640 / 005EE680, on the input side at +0x94): Ctrl (VK_CONTROL)
    // pressed (not a repeat) or released redraws the described item, so that the carried spell is
    // shown while it is held.
    void OnKeyDown(int key, int repeat, int flags);
    void OnKeyUp(int key, int repeat, int flags);
    // 005EE5C0 (slot 20). Draws the described item's model at (x + 100, y + 200), size 120,
    // greyed (mode 9) through the owner's DrawItem; returns 1 (ui_item_view.cpp).
    BOOL Draw(int pass);
    // 005EE440 (RET 8). Builds the panel at layout point (x, y): its 200 x 300 surface (format 5),
    // the item texts "infoitem_0" .. "_44" and the skill texts "infoskill_0" .. "_24" from the text
    // table, and an empty item list (its slot 2).
    void Build(int x, int y);
    // 005EE380 (ui_window_delete.cpp). The destructor body: its vtables, the skill and item text
    // arrays, the item list (00661050 class's 006611D0), then the window's.
    void DestroyBody();
    // 005EE420 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CItemInfoPanel* ItemInfoPanelDelete(unsigned flags);

    // 005EE720 (ui_small_members.cpp; the Describe* members call it). The described item: the
    // list's first entry, or null.
    CItem* FirstItem() const;
    // The described item (005EE720 inlined): the list's first entry, or null.
    CItem* Item() const { return itemCount_ ? items_[0] : nullptr; }
    // The item list as the party it is (itemList_ through itemCount_).
    CParty* ItemParty() { return reinterpret_cast<CParty*>(itemList_); }

    std::uint8_t itemList_[0x40];    // +0x150 an item list (0x70 bytes, 00661050 class); its
    CItem** items_;                  // +0x190   entries
    int itemCount_;                  // +0x194
    std::uint8_t unused_198[0x1C0 - 0x198];
    int x_;                          // +0x1C0 layout position of the surface
    int y_;                          // +0x1C4
    mfc::CStringArray itemTexts_;    // +0x1C8 "infoitem_0" .. "infoitem_44"
    mfc::CStringArray skillTexts_;   // +0x1DC "infoskill_0" .. "infoskill_24"
};
static_assert(sizeof(CItemInfoPanel) == 0x1F0);
static_assert(offsetof(CItemInfoPanel, owner_) == 0x14C);
static_assert(offsetof(CItemInfoPanel, items_) == 0x190);
static_assert(offsetof(CItemInfoPanel, x_) == 0x1C0);
static_assert(offsetof(CItemInfoPanel, itemTexts_) == 0x1C8);
static_assert(offsetof(CItemInfoPanel, skillTexts_) == 0x1DC);
