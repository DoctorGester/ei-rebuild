#pragma once

// The item description panel of the camp, the multiplayer hero screen and the exchange window
// (docs/notes/ui.md, "Item descriptions"). A UiWindow with one surface (200 x 300 at the layout point x,
// y) on which it writes what the mouse is over: an item (DescribeItem), a statistic of the hero or the
// level-up page, or a camp help topic. Its draw draws the described item's model through the owner screen.
// Every description writes the title wrapped in white at (15, 10)-(185, 25), then lines 15 pixels apart in
// kUiTextColour, the description text wrapped last.

#include "engine/core/core_refcount.h"
#include "game/players/party.h"
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include <map>
#include <vector>

class ItemInfoPanel;
class Texture;

// The seven damage values against the last one: the threshold ("%.1f": EI_Plugin's Fixes, always on, where the
// original printed the battle screen's as "%d"), the damage kinds ("dmg_<n>" texts, ", " separated) of the first six
// above it and those below it (or unordered).
struct DamageLists {
    std::string threshold;
    std::string above;
    std::string below;
};
DamageLists ItemDamageLists(const float* values);

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
    Item* item = nullptr;
    float x = 0.0f;  // the cell's centre (layout)
    float y = 0.0f;
    float depth = 0.0f;
    float size = 0.0f;  // the cell (100 in the strips and grids, 120 in the description panel)
    int mode = 0;    // the strip's action mode; 9: greyed out, never shaded as unusable
};

// A hero's face drawn in a cell (the hero-face draw's argument).
struct HeroFaceCell {
    std::string_view prototype;  // the unit prototype's name
    float x = 0.0f;          // the cell's centre (layout)
    float y = 0.0f;
    float depth = 0.0f;
    float size = 0.0f;
};

// A cached object (counted) and when it was last used (timeGetTime).
template <typename Object>
struct ItemCacheEntry {
    Object* object = nullptr;
    std::uint32_t lastUse = 0;
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
// platform::FrameClock, for TrimItemCache (defined in ui_item_info.cpp, which includes platform/platform.h for it).
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

// An item a constructor made: a move into a list shown turning sets its turn's flag.
inline bool IsMade(Item* item) {
    const ItemTurn* turn = item->turn;
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

// What an item panel has under a point (UiItemPanel::ItemAt): the item, or null, and the help topic of the place
// (0: none).
struct ItemUnder {
    Item* item = nullptr;
    int topic = 0;
};

// The panels of the item screens (camp, multiplayer hero screen, exchange): the description panel, hero
// equipment, quick strip, trade and repair grids, constructor pages and item strips, built on the blank
// panel. They share the left-button handler; ItemAt gives the item and help topic under the
// mouse.
class UiItemPanel : public UiWindow {
public:
    UiItemPanel(ItemScreen* newOwner) : owner(newOwner) {}
    // The camp's and the hero screen's panels also keep their owner as that screen (camp, heroScreen).
    UiItemPanel(CampPages* newCamp);
    UiItemPanel(MpHeroScreen* newHeroScreen);
    // What lies under the screen point: the item, or null, and the help topic of its place ("camphelp <topic>"; 0
    // for none). The panels without items of their own (description panel, statistics column, trade panel, the
    // figure strips): nothing.
    virtual ItemUnder ItemAt(Point point);

    void OnEvent(UiEvent& event) override;
    // The item under (x, y) goes to the owner with mode; without one, a help topic the panel named goes to
    // the camp's help handler (nothing on the other screens). The event is not marked used.
    void LeftDown(UiEvent& event);

    int mode = 0;                    // the action mode handed to the owner (ItemCell::mode)
    ItemScreen* owner = nullptr;
    CampPages* camp = nullptr;            // the owner when it is the camp, else null
    MpHeroScreen* heroScreen = nullptr;   // the owner when it is the multiplayer hero screen, else null
};

// The item screens' icon window.
class ItemIconWindow final : public UiWindow {
public:
    // The frame sprites: a strip of 8 cells over the layout's middle, the column edges and corners of four
    // 200-wide columns, and six bars.
    void Build() override;
    // Places the two bars across the left and right columns (0..100, 100..200) at y - 20..y + 20 (depth 7.8,
    // scale 100); the exchange window passes 300.
    void PlaceBars(int y);

    UiMesh* leftBar = nullptr;      // the first two of the six bars
    UiMesh* rightBar = nullptr;
};

// The item screens (the camp, the multiplayer hero screen and the exchange window share these fields): the
// description panel, the item under the mouse, and the caches of item models (by the item's picture name) and
// figure textures (by its figure name).
class ItemScreen : public UiContainer {
public:
    ~ItemScreen() override;

    // The item screens' slots over the container's: the plain item screen's answers and empty stubs; the
    // camp, the exchange and the hero screen override them. What one screen alone answers its panels ask it
    // directly (UiItemPanel::camp, heroScreen).
    virtual void TakeItem(Item*, int) {}
    virtual bool CanEquip(Item* item, int mode);
    virtual void RefreshStats() {}
    virtual PartyUnitRecord* HeroRecord() { return nullptr; }               // the shown hero
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
    ItemScreen() {}
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
    void OnEvent(UiEvent& event) override;
    // Ctrl (platform::Key::Control) pressed (not a repeat) or released redraws the described item, so that the
    // carried spell is shown while it is held.
    void KeyDown(UiEvent& event);
    void KeyUp(UiEvent& event);
    // Draws the described item's model at (x + 100, y + 200), size 120, greyed (mode 9) through the owner's DrawItem.
    void DrawSprites(SceneRenderer* pass) override;
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
    std::vector<UiLabel> labels;            // the surface's paint: the description DescribeItem and its callers wrote
};
