// The hero panels' small methods: the equipment panel's shown weapon / quick item,
// its right button and item lookup; the quick strip's selection
// and item lookup. See docs/notes/ui.md, section 5.
#include "ui_camp.h"
#include "ui_screens.h"
#include "item.h"
#include "unit.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

const char* const kChangeSound = "buttons\\camp\\change.wav";

constexpr int kWeaponSlots = 4;
constexpr int kQuickSlots = 8;
constexpr int kFirstBodySlot = 8;
constexpr int kBodySlotEnd = 15;
constexpr int kTopicWeapon = 2;
constexpr int kTopicQuickItem = 3;
constexpr int kTopicEntry = 1;

// `item`'s place in `list`, or -1.
int IndexIn(const std::vector<CItem*>& list, const CItem* item) {
    auto found = std::ranges::find(list, item);
    return found == list.end() ? -1 : static_cast<int>(found - list.begin());
}

} // namespace

// --- the shown entries ---------------------------------------------------------------------------

void CHeroEquipPanel::ShowWeapon(CItem* weapon) {
    PartyUnitRecord* hero = owner->HeroRecord();
    hero->weaponIndex = static_cast<std::uint32_t>(IndexIn(hero->weapons, weapon));
}

void CHeroEquipPanel::ShowQuickItem(CItem* item) {
    PartyUnitRecord* hero = owner->HeroRecord();
    selected = IndexIn(hero->quickItems, item);
}

void CHeroQuickStrip::ShowEntry(CItem* item) {
    PartyUnitRecord* hero = owner->HeroRecord();
    selected = IndexIn(hero->spells, item);
}

// --- the equipment panel's right button ----------------------------------------------------------

void CHeroEquipPanel::RightDown(int, int x, int y) {
    if (!body || !enabled) return;
    int region = RegionAt(x, y);
    if (region < 0) return;
    PartyUnitRecord* hero = owner->HeroRecord();
    if (region < std::ssize(hero->weapons) && region < kWeaponSlots) {
        ui->PlaySound(kChangeSound);
        hero->weaponIndex = static_cast<std::uint32_t>(region);
    }
    owner->RefreshWeapons();
}

// --- the item under a point ----------------------------------------------------------------------

CItem* CHeroEquipPanel::ItemAt(int x, int y, int* topic) {
    *topic = 0;
    if (!enabled) return nullptr;
    int region = RegionAt(x, y);
    if (region < 0) return nullptr;
    PartyUnitRecord* hero = owner->HeroRecord();
    if (region < kWeaponSlots) {
        *topic = kTopicWeapon;
        if (region < std::ssize(hero->weapons)) return hero->weapons[region];
    } else if (region < kQuickSlots) {
        *topic = kTopicQuickItem;
        int fromEnd = kQuickSlots - 1 - region;
        if (fromEnd < std::ssize(hero->quickItems)) return hero->quickItems[fromEnd];
    }
    if (!body || region < kFirstBodySlot || region >= kBodySlotEnd) return nullptr;
    *topic = region - 4;
    for (int i = 0; i < std::ssize(hero->armor); ++i) {
        CItemArmor* armour = AsArmor(hero->armor[i]);
        if (armour->GetType() == region - kFirstBodySlot) return armour;
    }
    return nullptr;
}

CItem* CHeroQuickStrip::ItemAt(int x, int atY, int* topic) {
    *topic = 0;
    if (!enabled) return nullptr;
    int region = RegionAt(x, atY);
    PartyUnitRecord* hero = owner->HeroRecord();
    if (region < 0 || region >= kQuickSlots) return nullptr;
    *topic = kTopicEntry;
    if (region >= std::ssize(hero->spells)) return nullptr;
    return hero->spells[region];
}
