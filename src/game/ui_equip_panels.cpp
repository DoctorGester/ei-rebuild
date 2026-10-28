// The hero panels' small methods: the equipment panel's shown weapon / item,
// its right button and item lookup; the quick strip's selection
// and item lookup. See docs/notes/ui.md, section 5.
#include "ui_camp.h"
#include "ui_screens.h"
#include "item.h"
#include "unit.h"

#include <cstdint>
#include <vector>

namespace {

const char* ChangeSound() { return "buttons\\camp\\change.wav"; }

constexpr int kWeaponSlots = 4;
constexpr int kQuickSlots = 8;
constexpr int kFirstBodySlot = 8;
constexpr int kBodySlotEnd = 15;
constexpr int kTopicWeapon = 2;
constexpr int kTopicSecond = 3;
constexpr int kTopicEntry = 1;

// `item`'s place in `list`, or -1.
int IndexIn(const std::vector<CItem*>& list, const CItem* item) {
    for (int i = 0; i < std::ssize(list); ++i)
        if (list[i] == item) return i;
    return -1;
}

void RefreshWeapons(CUiWindow* owner) { static_cast<CItemScreen*>(owner)->RefreshWeapons(); }

} // namespace

// --- the shown entries ---------------------------------------------------------------------------

void CHeroEquipPanel::ShowWeapon(CItem* weapon) {
    PartyUnitRecord* hero = owner_->HeroRecord();
    hero->weaponIndex = static_cast<std::uint32_t>(-1);
    int index = IndexIn(hero->weapons, weapon);
    if (index >= 0) hero->weaponIndex = static_cast<std::uint32_t>(index);
}

void CHeroEquipPanel::ShowArmour(CItem* item) {
    PartyUnitRecord* hero = owner_->HeroRecord();
    selected_ = -1;
    int index = IndexIn(hero->quickItems, item);
    if (index >= 0) selected_ = index;
}

void CHeroQuickStrip::ShowEntry(CItem* item) {
    PartyUnitRecord* hero = owner_->HeroRecord();
    selected_ = -1;
    int index = IndexIn(hero->spells, item);
    if (index >= 0) selected_ = index;
}

// --- the equipment panel's right button ----------------------------------------------------------

void CHeroEquipPanel::OnRightDown(int, int x, int y) {
    if (!body_ || !enabled_) return;
    int region = RegionAt(x, y);
    if (region < 0) return;
    PartyUnitRecord* hero = owner_->HeroRecord();
    if (region < std::ssize(hero->weapons) && region < kWeaponSlots) {
        interface_->PlaySound(ChangeSound(), 0);
        hero->weaponIndex = static_cast<std::uint32_t>(region);
    }
    RefreshWeapons(owner_);
}

// --- the item under a point ----------------------------------------------------------------------

CItem* CHeroEquipPanel::ItemAt(int x, int y, int* topic) {
    *topic = 0;
    if (!enabled_) return nullptr;
    int region = RegionAt(x, y);
    if (region < 0) return nullptr;
    PartyUnitRecord* hero = owner_->HeroRecord();
    if (region < kWeaponSlots) {
        *topic = kTopicWeapon;
        if (region < std::ssize(hero->weapons)) return hero->weapons[region];
    } else if (region < kQuickSlots) {
        *topic = kTopicSecond;
        int fromEnd = kQuickSlots - 1 - region;
        if (fromEnd < std::ssize(hero->quickItems)) return hero->quickItems[fromEnd];
    }
    if (!body_ || region < kFirstBodySlot || region >= kBodySlotEnd) return nullptr;
    *topic = region - 4;
    for (int i = 0; i < std::ssize(hero->armor); ++i) {
        CItemArmor* armour = AsArmor(hero->armor[i]);
        if (armour->GetType() == region - kFirstBodySlot) return armour;
    }
    return nullptr;
}

CItem* CHeroQuickStrip::ItemAt(int x, int y, int* topic) {
    *topic = 0;
    if (!enabled_) return nullptr;
    int region = RegionAt(x, y);
    PartyUnitRecord* hero = owner_->HeroRecord();
    if (region < 0 || region >= kQuickSlots) return nullptr;
    *topic = kTopicEntry;
    if (region >= std::ssize(hero->spells)) return nullptr;
    return hero->spells[region];
}
