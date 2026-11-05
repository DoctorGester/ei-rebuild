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
        ui->PlayInterfaceSound(kChangeSound);
        hero->weaponIndex = static_cast<std::uint32_t>(cell);
    }
    if (camp) camp->RefreshWeapons();
}

// --- the item under a point ----------------------------------------------------------------------

Item* HeroEquipPanel::ItemAt(Point point, int* topic) {
    *topic = 0;
    if (!enabled) return nullptr;
    const UiRegion* region = RegionUnder(point);
    if (!region) return nullptr;
    PartyUnitRecord* hero = owner->HeroRecord();
    if (const int cell = RegionIndex(cells, region); cell >= 0) {
        if (cell < kWeaponSlots) {
            *topic = kTopicWeapon;
            if (cell < std::ssize(hero->weapons)) return hero->weapons[cell];
        } else {
            *topic = kTopicQuickItem;
            int fromEnd = kCells - 1 - cell;
            if (fromEnd < std::ssize(hero->quickItems)) return hero->quickItems[fromEnd];
        }
        return nullptr;
    }
    const int part = RegionIndex(bodySlots, region);
    if (!body || part < 0) return nullptr;
    *topic = kTopicFirstBodyPart + part;
    for (int i = 0; i < std::ssize(hero->armor); ++i) {
        ItemArmor* armour = hero->armor[i]->AsArmor();
        if (static_cast<int>(armour->GetType()) == part) return armour;
    }
    return nullptr;
}

Item* HeroQuickStrip::ItemAt(Point point, int* topic) {
    *topic = 0;
    if (!enabled) return nullptr;
    const int cell = RegionIndex(cells, RegionUnder(point));
    PartyUnitRecord* hero = owner->HeroRecord();
    if (cell < 0) return nullptr;
    *topic = kTopicEntry;
    if (cell >= std::ssize(hero->spells)) return nullptr;
    return hero->spells[cell];
}
