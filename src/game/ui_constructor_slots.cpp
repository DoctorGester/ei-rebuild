// The constructors' second pages (CConstructorPage, ui_camp.h): the slot item under a point
// (slot 25), which the camp's hover and help ask every panel for. The spell maker 00628B50, the
// item maker 006298B0. See docs/notes/ui.md, section 5.
#include "ui_camp.h"

#include "item.h"
#include "party.h"

#include "ei/hook.h"
#include "originals_ui.h"

namespace {

// Help topics.
constexpr int kTopicNone = 0;
constexpr int kTopicSpellContainer = 0x13;
constexpr int kTopicSpell = 0x14;
constexpr int kTopicModifier = 0x15;
constexpr int kTopicSkillLine = 0x1A;
constexpr int kTopicManaLine = 0x1B;
constexpr int kTopicBuildable = 0xD;
constexpr int kTopicPart = 0xE;
constexpr int kTopicMaterial = 0xF;
constexpr int kTopicItemContainer = 0x10;

// The item predicates (005EE050-005EE130, through their addresses).
bool IsSpellContainer(const void* item) { return static_cast<std::uint8_t>(ItemKindIsSpellContainerOriginal(item)) != 0; }

// The `nth` (from 0) of the slots' items that `matches`, or null.
template <typename Predicate>
CItem* NthSlot(void* slots, int nth, Predicate matches) {
    const auto& items = static_cast<CParty*>(slots)->items_;
    int found = 0;
    for (int i = 0; i < items.size; ++i) {
        if (!matches(items.data[i])) continue;
        if (found == nth) return static_cast<CItem*>(items.data[i]);
        ++found;
    }
    return nullptr;
}

} // namespace

CItem* CConstructorPage::SpellMakerItemAt(int x, int y, int* topic) {
    *topic = kTopicNone;
    if (!enabled_) return nullptr;
    int region = RegionAt(x, y);
    if (region == 0) {
        *topic = kTopicSpellContainer;
        return NthSlot(slots_, 0, IsSpellContainer);
    }
    if (region == 1) {
        *topic = kTopicSpell;
        return NthSlot(slots_, 0, ItemKindIsSpellOriginal);
    }
    if (region >= 2 && region < 10) {
        *topic = kTopicModifier;
        if (CItem* item = NthSlot(slots_, region - 2, ItemKindIsModifierOriginal)) return item;
    }
    if (region == 10) *topic = kTopicSkillLine;
    else if (region == 11) *topic = kTopicManaLine;
    return nullptr;
}

CItem* CConstructorPage::ItemMakerItemAt(int x, int y, int* topic) {
    *topic = kTopicNone;
    if (!enabled_) return nullptr;
    int region = RegionAt(x, y);
    if (region == 0) {
        *topic = kTopicBuildable;
        return NthSlot(slots_, 0, ItemKindIsBuildableOriginal);
    }
    if (region == 1) {
        *topic = kTopicPart;
        return NthSlot(slots_, 0, ItemKindIsPartOriginal);
    }
    if (region == 2) {
        *topic = kTopicItemContainer;
        return NthSlot(slots_, 0, IsSpellContainer);
    }
    if (region > 2) {
        *topic = kTopicMaterial;
        return NthSlot(slots_, region - 3, ItemKindIsKindOneOriginal);
    }
    return nullptr;
}

namespace {
CItem* __fastcall SpellMakerItemAt(CConstructorPage* self, void*, int x, int y, int* topic) {
    return self->SpellMakerItemAt(x, y, topic);
}
CItem* __fastcall ItemMakerItemAt(CConstructorPage* self, void*, int x, int y, int* topic) {
    return self->ItemMakerItemAt(x, y, topic);
}
} // namespace
EI_REPLACE(0x00628B50, SpellMakerItemAt);
EI_REPLACE(0x006298B0, ItemMakerItemAt);
