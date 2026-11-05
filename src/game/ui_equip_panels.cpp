// The hero panels' small methods: the equipment panel's shown weapon / quick item,
// its right button and item lookup; the quick strip's selection
// and item lookup. See docs/notes/ui.md, section 5.
#include "game/ui_camp.h"
#include "game/ui_screens.h"
#include "game/item.h"
#include "game/unit.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

const char* const kChangeSound = "buttons\\camp\\change.wav";

constexpr int kWeaponSlots = 4;      // the cells before the quick items
constexpr int kTopicWeapon = 2;
constexpr int kTopicQuickItem = 3;
constexpr int kTopicFirstBodyPart = 4;   // plus the body part
constexpr int kTopicEntry = 1;

// `item`'s place in `list`, or -1.
int IndexIn(const std::vector<Item*>& list, const Item* item) {
    auto found = std::ranges::find(list, item);
    return found == list.end() ? -1 : static_cast<int>(found - list.begin());
}

} // namespace

// --- the shown entries ---------------------------------------------------------------------------

void HeroEquipPanel::ShowWeapon(Item* weapon) {
    PartyUnitRecord* hero = owner->HeroRecord();
    hero->weaponIndex = static_cast<std::uint32_t>(IndexIn(hero->weapons, weapon));
}

void HeroEquipPanel::ShowQuickItem(Item* item) {
    PartyUnitRecord* hero = owner->HeroRecord();
    selected = IndexIn(hero->quickItems, item);
}

void HeroQuickStrip::ShowEntry(Item* item) {
    PartyUnitRecord* hero = owner->HeroRecord();
    selected = IndexIn(hero->spells, item);
}

// --- the equipment panel's right button ----------------------------------------------------------

void HeroEquipPanel::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::RightDown: case UiEventKind::RightDouble: RightDown(event); break;
    default: UiItemPanel::OnEvent(event); break;
    }
}

void HeroEquipPanel::RightDown(UiEvent& event) {
    if (!body || !enabled) return;
    const UiRegion* region = RegionUnder(event.at);
    if (!region) return;
    PartyUnitRecord* hero = owner->HeroRecord();
    const int cell = RegionIndex(cells, region);
    if (cell >= 0 && cell < std::ssize(hero->weapons) && cell < kWeaponSlots) {
        g_interface.PlayInterfaceSound(kChangeSound);
        hero->weaponIndex = static_cast<std::uint32_t>(cell);
    }
    if (camp) camp->RefreshWeapons();
}

// --- the item under a point ----------------------------------------------------------------------

ItemUnder HeroEquipPanel::ItemAt(Point point) {
    if (!enabled) return {};
    const UiRegion* region = RegionUnder(point);
    if (!region) return {};
    PartyUnitRecord* hero = owner->HeroRecord();
    if (const int cell = RegionIndex(cells, region); cell >= 0) {
        if (cell < kWeaponSlots)
            return {cell < std::ssize(hero->weapons) ? hero->weapons[cell] : nullptr, kTopicWeapon};
        const int fromEnd = kCells - 1 - cell;
        return {fromEnd < std::ssize(hero->quickItems) ? hero->quickItems[fromEnd] : nullptr, kTopicQuickItem};
    }
    const int part = RegionIndex(bodySlots, region);
    if (!body || part < 0) return {};
    for (Item* worn : hero->armor) {
        ItemArmor* armour = worn->AsArmor();
        if (static_cast<int>(armour->GetType()) == part) return {armour, kTopicFirstBodyPart + part};
    }
    return {nullptr, kTopicFirstBodyPart + part};
}

ItemUnder HeroQuickStrip::ItemAt(Point point) {
    if (!enabled) return {};
    const int cell = RegionIndex(cells, RegionUnder(point));
    PartyUnitRecord* hero = owner->HeroRecord();
    if (cell < 0) return {};
    return {cell < std::ssize(hero->spells) ? hero->spells[cell] : nullptr, kTopicEntry};
}
