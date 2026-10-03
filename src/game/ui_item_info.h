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

#include "ui.h"

#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

class CItem;

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

class CItemInfoPanel : public CUiWindow {
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

    // The described item (005EE720 inlined): the list's first entry, or null.
    CItem* Item() const { return itemCount_ ? items_[0] : nullptr; }

    std::uint8_t unknown_148[4];
    CUiWindow* owner_;               // +0x14C the item screen (camp, hero screen, exchange)
    std::uint8_t itemList_[0x40];    // +0x150 an item list (0x70 bytes, 00661050 class); its
    CItem** items_;                  // +0x190   entries
    int itemCount_;                  // +0x194
    std::uint8_t unknown_198[0x1C0 - 0x198];
    int x_;                          // +0x1C0 layout position of the surface
    int y_;                          // +0x1C4
    void* itemTextsVtable_;          // +0x1C8 CStringArray "infoitem_0" .. "infoitem_44"
    mfc::CString* itemTexts_;        // +0x1CC
    std::uint8_t unknown_1D0[0x1E0 - 0x1D0];
    mfc::CString* skillTexts_;       // +0x1E0 data of the CStringArray at +0x1DC ("infoskill_0" .. "_24")
    std::uint8_t unknown_1E4[0x1F0 - 0x1E4];
};
static_assert(sizeof(CItemInfoPanel) == 0x1F0);
static_assert(offsetof(CItemInfoPanel, owner_) == 0x14C);
static_assert(offsetof(CItemInfoPanel, items_) == 0x190);
static_assert(offsetof(CItemInfoPanel, x_) == 0x1C0);
static_assert(offsetof(CItemInfoPanel, itemTexts_) == 0x1CC);
static_assert(offsetof(CItemInfoPanel, skillTexts_) == 0x1E0);
