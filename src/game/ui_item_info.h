#pragma once

// The item description panel of the camp, the multiplayer hero screen and the exchange window
// (docs/notes/ui.md, "Item descriptions"). A UiWindow with one surface (200 x 300 at the layout point x,
// y) on which it writes what the mouse is over: an item (DescribeItem), a statistic of the hero or the
// level-up page, or a camp help topic. Its draw draws the described item's model through the owner screen.
// Every description writes the title wrapped in white at (15, 10)-(185, 25), then lines 15 pixels apart in
// 0xA7D7E4, the description text wrapped last.

#include "core_refcount.h"
#include "party_storage.h"
#include "ui.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include <map>
#include <vector>

class ItemInfoPanel;
class Texture;

// The seven damage values against the last one: the threshold ("%d" when `integer`, else "%.1f"), the damage
// kinds ("dmg_<n>" texts, ", " separated) of the first six above it and those below it (or unordered).
struct DamageLists {
    std::string threshold;
    std::string above;
    std::string below;
};
DamageLists ItemDamageLists(const float* values, bool integer);

// --- The item screens' model view --------------------------------------------------------------

// A new 256 x 256 texture (texture manager, format 0x20, counted) holding image "<name>.mmp" decoded and
// packed to 1:5:5:5 at the bottom left (rows of 512 bytes), or white when there is no such image; null when
// it cannot be made or locked.
Texture* ItemFigureTexture(std::string_view name);
// The model manager's object `name`, or a 100 x 100 interface sprite standing in for it; counted (one
// reference for the caller).
UiMesh* ItemModel(std::string_view name);

// An item drawn as a turning model in a cell (DrawItem's argument).
struct ItemCell {
    Item* item;
    float x;         // the cell's centre (layout)
    float y;
    float depth;
    float size;      // the cell (100 in the strips and grids, 120 in the description panel)
    int mode;        // the strip's action mode; 9: greyed out, never shaded as unusable
};

// A hero's face drawn in a cell (the hero-face draw's argument).
struct HeroFaceCell {
    const char* prototype;   // the unit prototype's name
    float x;                 // the cell's centre (layout)
    float y;
    float depth;
    float size;
};

// A cached object (counted) and when it was last used (timeGetTime).
template <typename Object>
struct ItemCacheEntry {
    Object* object;
    std::uint32_t lastUse;
};

// A cache of the item screens (models and textures): a map from a name to {object, time of last use}, and
// how many entries it keeps (30; 20 when an option byte is set). TrimItemCache releases the least recently
// used objects beyond that.
template <typename Object>
struct ItemObjectCache {
    std::map<std::string, ItemCacheEntry<Object>> map;
    std::uint32_t limit = 50;
};
// Kept: the screens' two named caches of the one template.
using ItemModelCache = ItemObjectCache<UiMesh>;        // the models (UiMesh)
using ItemTextureCache = ItemObjectCache<Texture>;     // the model textures (ItemFigureTexture's)
// platform::Milliseconds, for TrimItemCache (defined in ui_item_info.cpp, which includes platform/platform.h for it).
std::uint32_t ItemCacheTime();
// While the cache holds more than its limit: the least recently used entry (used before now) has
// its object released and is erased. Spins while every entry was used at or after now.
template <typename Object>
inline void TrimItemCache(ItemObjectCache<Object>* cache) {
    while (cache->map.size() > cache->limit) {
        std::uint32_t oldestTime = ItemCacheTime();
        auto oldest = cache->map.end();
        for (auto node = cache->map.begin(); node != cache->map.end(); ++node) {
            if (node->second.lastUse < oldestTime) {
                oldestTime = node->second.lastUse;
                oldest = node;
            }
        }
        if (oldest != cache->map.end()) {
            oldest->second.object->Release();
            cache->map.erase(oldest);
        }
    }
}

// An item's turning state, a counted object in the item's link: made by the item draw and the moves.
class ItemTurn : public RefObject {
public:
    ItemTurn() {
        refs = 0;
        flags6 = 0;
        flags7 = 0;
    }
    void OnShutdown() override;
    ItemTurn* Clone() override;

    float angle = 0.0f;      // radians, wrapped at 2 pi
    float speed = 0.0f;
    bool flag = false;       // set by a move into a list shown turning
    float glow = 0.0f;       // phase of the spell glow
};
inline ItemTurn* ItemTurnOf(Item* item) { return static_cast<ItemTurn*>(item->link.Get()); }
// An item a constructor made: a move into a list shown turning sets its turn's flag.
inline bool IsMade(Item* item) {
    const ItemTurn* turn = ItemTurnOf(item);
    return turn && turn->flag;
}

// The party's best skill in a spell school and its most mana units (ItemScreen::BestPartySkill).
struct PartySkill {
    int skill = 0;
    int mana = 0;
};

// A camp deal's price: the index of the camp's weight for it (ClassTemplate::weights, CampPages::priceWeights). The
// constructors trade, make and take apart spells (0-3) and items (4-7); None shows no price (an item strip).
enum class PriceKind : int {
    SpellBuy, SpellSell, SpellMake, SpellApart,
    ItemBuy, ItemSell, ItemMake, ItemApart,
    Repair,
    None,
};

class Party;
class ItemScreen;
// The item screens' icon window.
class ItemIconWindow final : public UiWindow {
public:
    explicit ItemIconWindow(Interface3D* ui) : UiWindow(ui) {}
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
class ItemScreen : public UiContainer {
public:
    ~ItemScreen() override;

    // The item screens' slots over the container's: the plain item screen's answers and empty stubs; the
    // camp, the exchange and the hero screen override them.
    virtual void TakeItem(Item*, int) {}
    virtual bool CanEquip(Item* item, int mode);
    virtual void TopicClicked(int) {}
    virtual PartySkill BestPartySkill(int school);
    virtual void RefreshWeapons() {}
    virtual void RefreshStats() {}
    virtual void NextHero() {}
    virtual void PreviousHero() {}
    virtual Unit* HeroUnit(int index);
    virtual PartyUnitRecord* HeroRecord() { return nullptr; }               // the shown hero
    virtual void ChooseFigure(std::string_view) {}
    virtual float Weight();
    virtual int Price(Item* item, PriceKind kind);
    // Draws `cell`'s item: its model (cached, created) with its texture (cached) turning while the mouse is
    // on it, a glow in the colour of the carried spell's school, a frame behind it, the modifier marks of a
    // spell container, and a shade over items the strip's mode cannot take (CanEquip).
    void DrawItem(SceneRenderer* pass, const ItemCell& cell);
    // Draws the face of `cell`'s prototype: model "infa<model>me<n + 1>face" (the figure prototype's
    // model name, the prototype's field) with texture "face<m|f><two digits>" ("m" for model "unhuma",
    // "f" for "unhufe", else nothing), both cached as the items are, tilted back by (y - 300) / 1000
    // radians, over the cell frame.
    void DrawHeroFace(SceneRenderer* pass, const HeroFaceCell& cell);
    // The screens' common Create: the icon window (iconWindow, a child with the screen's texture 0, built), textures
    // "modifiers2" and "campslots", the display's settings, and the caches' limits (30, or 20 with an option byte
    // set).
    void CreateBase();
    // Moves `item` into `to` (Party::AddItem with the stack kept when `merge` and Shift are down, merging
    // with `merge`) and out of `from` (RemoveItem, all of the stack likewise); returns what `to` now holds.
    // With `limit` > 0 and at least that many items in `to`, only when `item` is the same (IsSameAs) as one
    // of its first `limit` items (else nothing, null). The unconditional move with `turning` gives the moved
    // item a turning state (made if missing) with its flag set.
    Item* MoveItem(Item* item, Party* to, Party* from, int limit, bool merge, bool turning);
    // Moves every item of `from` into `to` (whole stacks, merged); returns the last item `to` took, or null
    // when `from` was empty.
    Item* ReturnAll(Party* to, Party* from);
    ItemInfoPanel* descPanel = nullptr;
    Item* hoveredItem = nullptr;    // the item under the mouse (turns)
    ItemIconWindow* iconWindow = nullptr;
    ItemModelCache models;
    ItemTextureCache textures;

protected:
    explicit ItemScreen(Interface3D* ui) : UiContainer(ui) {}
};

// What a statistic under the mouse is (DescribeStats). The order matters: up to Perk the title and
// description come from text "perk <text>", after it from <text> and <text>_desc.
enum class HoverKind {
    None = -1,
    Attribute,               // index: the attribute
    Skill,                   // a magic school's skill (index: the school)
    Science,                 // index 0
    Perk,                    // index: the levels learned
    Armour,                  // a body part's armour (part)
    Weapon,                  // the weapon's attack or defence (weapon)
    Magic,                   // the magic skills
    Text,                    // a plain text
};

// What the statistics under the mouse are (filled by the stats panel and the trade panel, read by
// DescribeStats).
struct ItemHoverInfo {
    HoverKind kind = HoverKind::None;
    int index = 0;
    int part = 0;                    // body part (Armour)
    ItemWeapon* weapon = nullptr;   // Weapon
    UnitStats* stats = nullptr;      // the hero's statistics (in the hero record)
    std::string text;                // the text key
};

class ItemInfoPanel : public UiItemPanel {
public:
    using UiItemPanel::UiItemPanel;
    void Deactivate() override { DescribeItem(); }
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
    // Empties the list, clears the surface and, unless the kind is None, writes the statistic's title, values
    // and description.
    void DescribeStats(const ItemHoverInfo& info);
    // Empties the list, clears the surface and writes help topic "camphelp <topic>".
    void ShowHelp(int topic);
    // Describes `item` (null: nothing) unless it is already the described one (IsSameAs).
    void Describe(Item* item);
    // Ctrl (platform::kKeyControl) pressed (not a repeat) or released redraws the described item, so that the
    // carried spell is shown while it is held.
    void KeyDown(int key, int repeat, int flags) override;
    void KeyUp(int key, int repeat, int flags) override;
    // Draws the described item's model at (x + 100, y + 200), size 120, greyed (mode 9) through the owner's DrawItem.
    void Draw(SceneRenderer* pass) override;
    // Builds the panel at layout point (x, y): its 200 x 300 surface (format 5), the item texts
    // "infoitem_0" .. "_44" and the skill texts "infoskill_0" .. "_24" from the text table, and an empty
    // item list.
    void Build(int x, int y);

    // The described item: the first of the list, or null.
    Item* FirstItem() { return itemParty.items.empty() ? nullptr : itemParty.items[0]; }

    Party itemParty;                // the described item's list
    int x = 0;                       // layout position of the surface
    int y = 0;
    std::vector<std::string> itemTexts;     // "infoitem_0" .. "infoitem_44"
    std::vector<std::string> skillTexts;    // "infoskill_0" .. "infoskill_24"
};
