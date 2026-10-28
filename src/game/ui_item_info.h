#pragma once

// The item description panel of the camp, the multiplayer hero screen and the exchange window
// (docs/notes/ui.md, "Item descriptions"). A CUiWindow with one surface (200 x 300 at the layout point x_,
// y_) on which it writes what the mouse is over: an item (DescribeItem), a statistic of the hero or the
// level-up page, or a camp help topic. Its draw draws the described item's model through the owner screen.
// Every description writes the title wrapped in white at (15, 10)-(185, 25), then lines 15 pixels apart in
// 0xA7D7E4, the description text wrapped last.

#include "core_refcount.h"
#include "item.h"
#include "party_storage.h"
#include "ui.h"

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class CItemInfoPanel;
class CTexture;

// The seven damage values against the last one: into `out` (emptied) the threshold ("%d" when `integer`, else
// "%.1f"), the damage kinds ("dmg_<n>" texts, ", " separated) of the first six above it and those below it
// (or unordered).
void ItemDamageLists(const float* values, std::vector<std::string>* out, char integer);

// --- The item screens' model view --------------------------------------------------------------

// A new 256 x 256 texture (texture manager, format 0x20, counted) holding image "<name>.mmp" decoded and
// packed to 1:5:5:5 at the bottom left (rows of 512 bytes), or white when there is no such image; null when
// it cannot be made or locked.
CTexture* ItemFigureTexture(const char* name);
// The model manager's object `name`, or a 100 x 100 interface sprite standing in for it; counted (one
// reference for the caller).
CUiMesh* ItemModel(const char* name);

// An item drawn as a turning model in a cell (DrawItem's argument).
struct ItemCell {
    CItem* item;
    float x;         // the cell's centre (layout)
    float y;
    float depth;
    float size;      // the cell (100 in the strips and grids, 120 in the description panel)
    int mode;        // the strip's action mode; 9: greyed out, never shaded as unusable
};
static_assert(sizeof(ItemCell) == 0x18);

// A hero's face drawn in a cell (the hero-face draw's argument).
struct HeroFaceCell {
    const char* prototype;   // the unit prototype's name
    float x;                 // the cell's centre (layout)
    float y;
    float depth;
    float size;
};
static_assert(sizeof(HeroFaceCell) == 0x14);

// A cached object (counted) and when it was last used (timeGetTime).
template <typename Object>
struct ItemCacheEntry {
    Object* object;
    std::uint32_t lastUse;
};

// A cache of the item screens (models_ and textures_): a map from a name to {object, time of last use}, and
// how many entries it keeps (30; 20 when an option byte is set). TrimItemCache releases the least recently
// used objects beyond that.
template <typename Object>
struct ItemObjectCache {
    using Map = std::map<std::string, ItemCacheEntry<Object>>;

    Map map;
    std::uint32_t limit = 50;
};
using ItemModelCache = ItemObjectCache<CUiMesh>;        // the models (CUiMesh)
using ItemTextureCache = ItemObjectCache<CTexture>;     // the model textures (ItemFigureTexture's)
// Every cached object released, in key order (the hero screen's model and texture caches).
template <typename Object>
void ReleaseCachedObjects(ItemObjectCache<Object>* cache);

template <typename Object>
void TrimItemCache(ItemObjectCache<Object>* cache);

// An item's turning state, a counted object in the item's link_: made by the item draw and the moves.
class ItemTurn : public CRefObject {
public:
    ItemTurn() {
        refs_ = 0;
        flags6_ = 0;
        flags7_ = 0;
    }
    ~ItemTurn() override;
    int OnShutdown() override;
    ItemTurn* Clone() override;

    float angle;             // radians, wrapped at 2 pi
    float speed;
    bool flag;               // set by a move into a list shown turning
    std::uint8_t unused_11[3];
    float glow;              // phase of the spell glow
};
static_assert(sizeof(ItemTurn) == 0x18);
inline ItemTurn* ItemTurnOf(CItem* item) { return static_cast<ItemTurn*>(item->link_); }

class CParty;
class CItemScreen;
// The item screens' icon window.
class CItemIconWindow final : public CUiWindow {
public:
    explicit CItemIconWindow(CInterface3D* ui) : CUiWindow(ui) {}
    // The frame sprites: a strip of 8 cells over the layout's middle, the column edges and corners of four
    // 200-wide columns, and six bars.
    void Build() override;
    // Places the two bars (sprites 25, 26) across the left and right columns (0..100, 100..200) at y - 20..y
    // + 20 (depth 7.8, scale 100); the exchange window passes 300.
    void PlaceBars(int y);
};

// The item screens (the camp, the multiplayer hero screen and the exchange window share these fields): the
// description panel, the item under the mouse, and the caches of item models (by the item's picture name) and
// figure textures (by its figure name).
class CItemScreen : public CUiContainer {
public:
    ~CItemScreen() override;

    // The item screens' slots over the container's: the plain item screen's answers and empty stubs; the
    // camp, the exchange and the hero screen override them.
    virtual void TakeItem(CItem*, int) {}
    virtual bool CanEquip(CItem* item, int mode);
    virtual void TopicClicked(int, int) {}
    virtual void BestPartySkill(int school, int* skill, int* mana);
    virtual void RefreshWeapons() {}
    virtual void RefreshStats() {}
    virtual void NextHero() {}
    virtual void PreviousHero() {}
    virtual CUnit* HeroUnit(int index);
    virtual PartyUnitRecord* HeroRecord() { return nullptr; }               // the shown hero
    virtual void ChooseFigure(const char*) {}
    virtual double Weight();
    virtual int Price(CItem* item, int kind);
    // Draws `cell`'s item: its model (cached, created) with its texture (cached) turning while the mouse is
    // on it, a glow in the colour of the carried spell's school, a frame behind it, the modifier marks of a
    // spell container, and a shade over items the strip's mode cannot take (CanEquip).
    void DrawItem(CSceneRenderer* pass, ItemCell* cell);
    // Draws the face of `cell`'s prototype: model "infa<model>me<n + 1>face" (the figure prototype's
    // model name, the prototype's field) with texture "face<m|f><two digits>" ("m" for model "unhuma",
    // "f" for "unhufe", else nothing), both cached as the items are, tilted back by (y - 300) / 1000
    // radians, over the cell frame.
    void DrawHeroFace(CSceneRenderer* pass, HeroFaceCell* cell);
    static void FaceNames(const char* prototype, std::string* model, std::string* texture);
    // The screens' common Create: the icon window (iconWindow_, a child with the screen's texture 0, built), textures
    // "modifiers2" and "campslots", the display's settings, and the caches' limits (30, or 20 with an option byte
    // set).
    void CreateBase();
    // Moves `item` into `to` (CParty::AddItem with the stack kept when `merge` and Shift are down, merging
    // with `merge`) and out of `from` (RemoveItem, all of the stack likewise); returns what `to` now holds.
    // With `limit` > 0 and at least that many items in `to`, only when `item` is the same (IsSameAs) as one
    // of its first `limit` items (else nothing, null). The unconditional move with `turning` gives the moved
    // item a turning state (made if missing) with its flag set. Flags are bytes.
    CItem* MoveItem(CItem* item, CParty* to, CParty* from, int limit, int merge, int turning);
    // Moves every item of `from` into `to` (whole stacks, merged); returns the last item `to` took, or null
    // when `from` was empty.
    CItem* ReturnAll(CParty* to, CParty* from);
    CItemInfoPanel* descPanel_;
    CItem* hoveredItem_;             // the item under the mouse (turns)
    CItemIconWindow* iconWindow_;
    ItemModelCache models_;
    ItemTextureCache textures_;

protected:
    explicit CItemScreen(CInterface3D* ui) : CUiContainer(ui), hoveredItem_(nullptr) {}
};

inline void CUiItemPanel::OwnerTakeItem(CItem* item) { owner_->TakeItem(item, mode_); }
inline void CUiItemPanel::OwnerTopicClicked(int topic) { owner_->TopicClicked(topic, mode_); }

// What the statistics under the mouse are (filled by the stats panel and the trade panel, read by
// DescribeStats).
struct ItemHoverInfo {
    int kind;                // -1 none; 0 an attribute, 1 a skill, 2 a skill school, 3 a perk,
                             // 4 a body part's armour, 5 the weapon's attack or defence,
                             // 6 the magic skills, 7 a plain text
    int index;               // the attribute, skill, school or perks learned
    int part;                // body part (kind 4)
    CItemWeapon* weapon;     // kind 5
    UnitStats* stats;        // the hero's statistics (in the hero record)
    std::string text;       // the text key
};
static_assert(offsetof(ItemHoverInfo, stats) == 0x10);

class CItemInfoPanel : public CUiItemPanel {
public:
    CItemInfoPanel(CInterface3D* ui, CItemScreen* owner);
    ~CItemInfoPanel() override;
    void Deactivate() override { DescribeItem(); }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    int KeyUp(int k, int r, int f) override { return OnKeyUp(k, r, f), 0; }
    // Clears the surface and describes the item in the list (nothing without one): its own description
    // for blueprints, materials, spells, modifiers, quick items of category 8 and spells carried by an
    // item while the modifier key is held or spell containers; else the common description (name, kind,
    // material, price, condition, damage, armour, carried spell).
    void DescribeItem();
    void DescribeMaterial();
    void DescribeBlueprint();
    void DescribeSpellRecord();
    void DescribeModifierRecord();
    void DescribeQuickItem();
    void DescribeCarriedSpell();
    // Empties the list, clears the surface and, for a kind 0-7, writes the statistic's title, values and description.
    void DescribeStats(ItemHoverInfo* info);
    // Empties the list, clears the surface and writes help topic "camphelp <topic>".
    void ShowHelp(int topic);
    // Describes `item` (null: nothing) unless it is already the described one (IsSameAs).
    void Describe(CItem* item);
    // Ctrl (VK_CONTROL) pressed (not a repeat) or released redraws the described item, so that the
    // carried spell is shown while it is held.
    void OnKeyDown(int key, int repeat, int flags);
    void OnKeyUp(int key, int repeat, int flags);
    // Draws the described item's model at (x + 100, y + 200), size 120, greyed (mode 9) through the owner's DrawItem;
    // returns 1.
    BOOL Draw(CSceneRenderer* pass) override;
    // Builds the panel at layout point (x, y): its 200 x 300 surface (format 5), the item texts
    // "infoitem_0" .. "_44" and the skill texts "infoskill_0" .. "_24" from the text table, and an empty
    // item list.
    void Build(int x, int y);

    CItem* FirstItem();
    CItem* Item() { return FirstItem(); }
    CParty* ItemParty() { return itemParty_.Get(); }

    PartyStorage itemParty_;         // the described item's list: an embedded CParty, built and destroyed by this panel
    int x_;                          // layout position of the surface
    int y_;
    std::vector<std::string> itemTexts_;    // "infoitem_0" .. "infoitem_44"
    std::vector<std::string> skillTexts_;   // "infoskill_0" .. "infoskill_24"
};
