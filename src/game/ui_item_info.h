#pragma once

// The item description panel of the camp, the multiplayer hero screen and the exchange window
// (see docs/notes/ui.md, "Item descriptions"). Names provisional.
// The panel is a
// CUiWindow with one surface (200 x 300 at the layout point +0x1C0, +0x1C4) on which it writes
// what the mouse is over: an item (DescribeItem puts it in the panel's item list and
// describes it, dispatching on the item's kind), a statistic of the hero or the level-up page
// or a camp help topic. Its slot 20 draws the described item's
// model through the owner screen (ui_item_view.cpp).
// Every description writes into the surface with the same layout: the title wrapped in white at
// (15, 10)-(185, 25), then lines 15 pixels apart in 0xA7D7E4, the description text wrapped last.

#include "core_refcount.h"
#include "item.h"
#include "ui.h"

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CItem;
class CItemInfoPanel;
class CTexture;   // texture.h
struct UnitStats;
namespace console {
class CStringArray;
}

// The seven damage values against the last one: into `out` (emptied)
// the threshold ("%d" when `integer`, else "%.1f"), the damage kinds ("dmg_<n>" texts, ", "
// separated) of the first six above it and those below it (or unordered).
void __fastcall ItemDamageLists(const float* values, console::CStringArray* out, char integer);

// --- The item screens' model view (ui_item_view.cpp) ------------------------------------

// A new 256 x 256 texture (texture manager, format 0x20, counted)
// holding image "<name>.mmp" decoded and packed to 1:5:5:5 at the bottom left (rows of 512 bytes),
// or white when there is no such image; null when it cannot be made or locked.
CTexture* __fastcall ItemFigureTexture(const char* name);
// ui_item_model.cpp. The model manager's object `name`, or a 100 x 100
// interface sprite standing in for it; counted (one reference for the caller).
CUiMesh* __fastcall ItemModel(const char* name);

// An item drawn as a turning model in a cell (DrawItem's argument, 0x18 bytes).
struct ItemCell {
    CItem* item;     // +0x00
    float x;         // +0x04 the cell's centre (layout)
    float y;         // +0x08
    float depth;     // +0x0C
    float size;      // +0x10 the cell (100 in the strips and grids, 120 in the description panel)
    int mode;        // +0x14 the strip's action mode; 9: greyed out, never shaded as unusable
};
static_assert(sizeof(ItemCell) == 0x18);

// A hero's face drawn in a cell (the hero-face draw's argument, 0x14 bytes).
struct HeroFaceCell {
    const char* prototype;   // +0x00 the unit prototype's name
    float x;                 // +0x04 the cell's centre (layout)
    float y;                 // +0x08
    float depth;             // +0x0C
    float size;              // +0x10
};
static_assert(sizeof(HeroFaceCell) == 0x14);

// A cached object and when it was last used (timeGetTime).
struct ItemCacheEntry {
    CEngineObject* object;   // a CUiMesh (models) or a CTexture (textures), counted
    std::uint32_t lastUse;
};

// A cache of the item screens (0x10 bytes at +0x170 and +0x180): an STLport map from a name to
// {object, time of last use} (0x24-byte nodes; lib_trees::ItemCache registers its members), and
// how many entries it keeps (30; 20 when an option byte is set). The two trim functions release the least
// recently used objects beyond that.
struct ItemObjectCache {
    using Map = stlp::map<stlp::string, ItemCacheEntry>;
    using Node = Map::Node;

    // The camp screen's constructor (and the hero screen's copy of it): the map's header
    // from the pool, emptied
    // with `compare` as its compare byte; the limit 50 (ui_item_screen.cpp).
    void Construct(std::uint8_t compare);

    Map map;
    std::uint32_t limit;   // +0x0C
};
static_assert(sizeof(ItemObjectCache) == 0x10 && sizeof(ItemObjectCache::Node) == 0x24);
// ui_hero_screen.cpp. Every cached object released
//, in key order (the hero screen's model and texture caches).
void __fastcall ReleaseCachedObjects(ItemObjectCache* cache);

// Release the least recently used entries of the
// model / texture cache beyond its limit (ui_item_screen.cpp).
void __fastcall TrimItemCache(ItemObjectCache* cache);

// An item's turning state (0x18 bytes), a counted object at the item's +0x1C (CItem::link_):
// made by the item draw and the moves.
class ItemTurn : public CRefObject {
public:
    // The count and the flags cleared (the original writes them as one dword); the rest is the maker's.
    ItemTurn() {
        refs_ = 0;
        flags6_ = 0;
        flags7_ = 0;
    }
    // The counted root's body (core_streams.cpp).
    ~ItemTurn() override;
    // ui_small_members.cpp. The original writes the object's tables again (a construction in
    // place): nothing for a C++ object. 0.
    int OnShutdown() override;
    // ui_item_screen.cpp. A new state with the same angle, speed, flag and glow and no
    // references (null when the allocation fails).
    ItemTurn* Clone() override;

    float angle;             // +0x08 radians, wrapped at 2 pi
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
struct PartyUnitRecord;
// The item screens' icon window (0x148 bytes). Its
// destructors are the window's.
class CItemIconWindow final : public CUiWindow {
public:
    explicit CItemIconWindow(CInterface3D* ui) : CUiWindow(ui) {}
    // The frame sprites: a strip of 8 cells over the layout's middle, the
    // column edges and corners of four 200-wide columns, and six bars.
    void Build() override;
    // The exchange window with 300. Places the two bars (sprites 25, 26) across
    // the left and right columns (0..100, 100..200) at y - 20..y + 20 (depth 7.8, scale 100).
    void PlaceBars(int y);
};

// The item screens (the camp, the multiplayer hero screen and the exchange window share these
// fields): the description panel, the item under the mouse, and the
// caches of item models (by the item's picture name) and figure textures (by its figure name).
class CItemScreen : public CUiContainer {
public:
    // ui_item_screen.cpp: both caches'
    // objects released and their maps destroyed, then the container's.
    ~CItemScreen() override;

    // The item screens' slots over the container's (the plain item screen,
    // whose 25 / 26 are pure; the camp, the exchange and the hero screen override them): what the
    // item strips and panels call on their owner. The plain item screen's answers (ui_small_members.cpp)
    // and empty stubs.
    virtual void TakeItem(CItem*, int) {}
    // No item may be moved (AL 0).
    virtual bool CanEquip(CItem* item, int mode);
    virtual void TopicClicked(int, int) {}
    // No skill and no mana.
    virtual void BestPartySkill(int school, int* skill, int* mana);
    virtual void RefreshWeapons() {}
    virtual void RefreshStats() {}
    virtual void NextHero() {}
    virtual void PreviousHero() {}
    // No hero unit.
    virtual CUnit* HeroUnit(int index);
    virtual PartyUnitRecord* HeroRecord() { return nullptr; }               // the shown hero
    virtual void ChooseFigure(const char*) {}
    // No weight (0.0, in ST0).
    virtual double Weight();
    // No price.
    virtual int Price(CItem* item, int kind);
    // Draws `cell`'s item: its model (cached, created) with its
    // texture (cached,) turning while the mouse is on it, a glow in the colour of the
    // carried spell's school, a frame behind it, the modifier marks of a spell container, and a
    // shade over items the strip's mode cannot take (CanEquip).
    void DrawItem(int pass, ItemCell* cell);
    // Draws the face of `cell`'s prototype: model "infa<model>me<n + 1>face" (the
    // figure prototype's +0xE8 model name, the prototype's +0x10) with texture "face<m|f><+0xC,
    // two digits>" ("m" for model "unhuma", "f" for "unhufe", else nothing), both cached as the
    // items are, tilted back by (y - 300) / 1000 radians, over the cell frame.
    void DrawHeroFace(int pass, HeroFaceCell* cell);
    // The face's model and texture names (the lookups insert unknown names as DrawHeroFace's do).
    static void FaceNames(const char* prototype, mfc::CString* model, mfc::CString* texture);
    // The screens' common Create: the icon window (+0x16C, CItemIconWindow, a child
    // with the screen's texture 0, built), textures "modifiers2" and "campslots", the display's
    // settings, and the caches' limits (30, or 20 with an option byte set) applied (ui_item_screen.cpp).
    void CreateBase();
    // ui_item_screen.cpp. Moves `item` into `to` (CParty::AddItem with the
    // stack kept when `merge` and Shift (CInterface3D +0x7D) are down, merging with `merge`) and
    // out of `from` (RemoveItem, all of the stack likewise); returns what `to` now holds. With
    // `limit` > 0 and at least that many items in `to`, only when `item` is the same (IsSameAs) as
    // one of its first `limit` items (else nothing, null). The unconditional move with `turning`
    // gives the moved item a turning state (made if missing) with its flag set. Flags are bytes.
    CItem* MoveItem(CItem* item, CParty* to, CParty* from, int limit, int merge, int turning);
    // ui_item_screen.cpp. Moves every item of `from` into `to` (whole stacks,
    // merged); returns the last item `to` took, or null when `from` was empty.
    CItem* ReturnAll(CParty* to, CParty* from);
    CItemInfoPanel* descPanel_;      // +0x164
    CItem* hoveredItem_;             // +0x168 the item under the mouse (turns)
    CItemIconWindow* iconWindow_;    // +0x16C
    ItemObjectCache models_;         // +0x170
    ItemObjectCache textures_;       // +0x180

protected:
    // Each screen's constructor starts with the container and no item under the mouse; the caches
    // are its own to set up.
    explicit CItemScreen(CInterface3D* ui) : CUiContainer(ui), hoveredItem_(nullptr) {}
};
static_assert(offsetof(CItemScreen, hoveredItem_) == 0x168);
static_assert(sizeof(CItemIconWindow) == 0x148);

inline void CUiItemPanel::OwnerTakeItem(CItem* item) { owner_->TakeItem(item, mode_); }
inline void CUiItemPanel::OwnerTopicClicked(int topic) { owner_->TopicClicked(topic, mode_); }
static_assert(offsetof(CItemScreen, models_) == 0x170);
static_assert(offsetof(CItemScreen, textures_) == 0x180);

// What the statistics under the mouse are (0x18 bytes; filled by the stats panel and the
// trade panel, read by DescribeStats).
struct ItemHoverInfo {
    int kind;                // +0x00 -1 none; 0 an attribute, 1 a skill, 2 a skill school, 3 a perk,
                             //       4 a body part's armour, 5 the weapon's attack or defence,
                             //       6 the magic skills, 7 a plain text
    int index;               // +0x04 the attribute, skill, school or perks learned
    int part;                // +0x08 body part (kind 4)
    CItemWeapon* weapon;     // +0x0C kind 5
    UnitStats* stats;        // +0x10 the hero's statistics (hero record + 0x20)
    mfc::CString text;       // +0x14 the text key
};
static_assert(sizeof(ItemHoverInfo) == 0x18);
static_assert(offsetof(ItemHoverInfo, stats) == 0x10);

class CItemInfoPanel : public CUiItemPanel {
public:
    // The description panel of `owner` (ui_item_info.cpp): its item
    // list and text arrays constructed.
    CItemInfoPanel(CInterface3D* ui, CItemScreen* owner);
    // ui_window_delete.cpp: the skill and item text arrays, the item
    // list, then the window's.
    ~CItemInfoPanel() override;
    void Deactivate() override { DescribeItem(); }                                    // input 1
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }       // input 9
    int KeyUp(int k, int r, int f) override { return OnKeyUp(k, r, f), 0; }           // input 11
    // Clears the surface and describes the item in the list (nothing without one):
    // its own description for blueprints, materials, spells,
    // modifiers, quick items of category 8 and spells carried by an item
    // while the modifier key +0x7C is held or spell containers; else the common
    // description (name, kind, material, price, condition, damage, armour, carried spell).
    void DescribeItem();
    void DescribeMaterial();
    void DescribeBlueprint();
    void DescribeSpellRecord();
    void DescribeModifierRecord();
    void DescribeQuickItem();
    void DescribeCarriedSpell();
    // Empties the list, clears the surface (DescribeItem) and, for a kind 0-7, writes the
    // statistic's title, values and description.
    void DescribeStats(ItemHoverInfo* info);
    // Empties the list, clears the surface and writes help topic "camphelp <topic>".
    void ShowHelp(int topic);
    // Describes `item` (null: nothing) unless it is already the described one (the
    // described item's IsSameAs).
    void Describe(CItem* item);
    // Input slots 9 / 11 (on the input side at +0x94): Ctrl (VK_CONTROL)
    // pressed (not a repeat) or released redraws the described item, so that the carried spell is
    // shown while it is held.
    void OnKeyDown(int key, int repeat, int flags);
    void OnKeyUp(int key, int repeat, int flags);
    // Draws the described item's model at (x + 100, y + 200), size 120,
    // greyed (mode 9) through the owner's DrawItem; returns 1 (ui_item_view.cpp).
    BOOL Draw(int pass) override;
    // Builds the panel at layout point (x, y): its 200 x 300 surface (format 5),
    // the item texts "infoitem_0" .. "_44" and the skill texts "infoskill_0" .. "_24" from the text
    // table, and an empty item list (its slot 2).
    void Build(int x, int y);

    // ui_small_members.cpp. The described item: the
    // list's first entry, or null.
    CItem* FirstItem() const;
    // The described item: the list's first entry, or null.
    CItem* Item() const { return itemCount_ ? items_[0] : nullptr; }
    // The item list as the party it is (itemList_ through itemCount_).
    CParty* ItemParty() { return reinterpret_cast<CParty*>(itemList_); }

    std::uint8_t itemList_[0x40];    // +0x150 an item list (0x70 bytes); its
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
