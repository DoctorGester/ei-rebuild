// Item lists: the party's item list and the party unit records' four item lists. See party.h, item.h, unit.h and
// docs/notes/items_spells.md ("Party item lists").
#include "item.h"
#include "party.h"
#include "spell.h"
#include "unit.h"
#include "world.h"
#include "lib/containers.h"

#include <cstdint>

namespace {

// A flag argument read as a byte.
bool ByteFlag(int flag) { return static_cast<std::uint8_t>(flag) != 0; }

// Appends `item` to `list` (SetSize(count + 1) and store, inlined).
void Append(std::vector<Item*>& list, Item* item) {
    int index = lib::Count(list);
    list.resize(index + 1);
    list[index] = item;
}

// Removes the element at `index`, closing the gap.
void RemoveAt(std::vector<Item*>& list, int index) { list.erase(list.begin() + index); }


Item* AddEntry(std::vector<Item*>& list, Item* item) {
    Item* entry = item->Clone(item->WorldContext()->GetSpellManager());
    entry->AddRef();
    Append(list, entry);
    return entry;
}

void RemoveEntry(std::vector<Item*>& list, Item* entry) {
    for (int i = 0; i < std::ssize(list); ++i) {
        if (list[i] != entry) continue;
        entry->Release();
        RemoveAt(list, i);
        return;
    }
}

// One running sum over the lists, in this order.
void AddListWeight(float& weight, const std::vector<Item*>& list) {
    for (int i = 0; i < std::ssize(list); ++i) {
        if (list[i]) weight += list[i]->Weight();
    }
}

} // namespace



// --- The party's items --------------------------------------------------------------------------

Item* Party::AddItem(Item* item, int keepCount, int merge) {
    if (!item) return nullptr;
    int count = ByteFlag(keepCount) ? static_cast<int>(item->count) : 1;
    if (ByteFlag(merge)) {
        for (int i = 0; i < std::ssize(items); ++i) {
            Item* existing = items[i];
            if (!existing->IsSameAs(item)) continue;
            existing->count += static_cast<std::uint32_t>(count);
            return existing;
        }
    }
    Item* copy = CopyItem(item);
    copy->count = static_cast<std::uint32_t>(count);
    Append(items, copy);
    copy->AddRef();
    return copy;
}

Item* Party::RemoveItem(Item* item, int all) {
    for (int i = 0; i < std::ssize(items); ++i) {
        if (items[i] != item) continue;
        Item* entry = items[i];
        if (static_cast<int>(entry->count) > 1 && !ByteFlag(all)) {
            --entry->count;
            return entry;
        }
        entry->Release();
        RemoveAt(items, i);
        return nullptr;
    }
    return nullptr;
}

void Party::RemoveQuestItems() {
    for (int i = lib::Count(items) - 1; i >= 0; --i) {
        Item* item = items[i];
        if (item->GetClassId() == ClassId::ItemQuest) RemoveItem(item, 1);
    }
}

void Party::ReleaseAllItems() {
    for (int i = 0; i < std::ssize(items); ++i) items[i]->Release();
    items.clear();
}

// --- The party unit records' item lists ---------------------------------------------------------

Item* PartyUnitRecord::AddWeapon(Item* item) { return AddEntry(weapons, item); }
Item* PartyUnitRecord::AddQuickItem(Item* item) { return AddEntry(quickItems, item); }
Item* PartyUnitRecord::AddArmor(Item* item) { return AddEntry(armor, item); }
Item* PartyUnitRecord::AddSpell(Item* item) { return AddEntry(spells, item); }

void PartyUnitRecord::RemoveWeapon(Item* entry) { RemoveEntry(weapons, entry); }
void PartyUnitRecord::RemoveQuickItem(Item* entry) { RemoveEntry(quickItems, entry); }
void PartyUnitRecord::RemoveArmor(Item* entry) { RemoveEntry(armor, entry); }
void PartyUnitRecord::RemoveSpell(Item* entry) { RemoveEntry(spells, entry); }

// --- The weight a unit record carries ---------------------------------------------

float PartyUnitRecord::CarriedWeight() const {
    float weight = 0.0f;
    AddListWeight(weight, weapons);
    AddListWeight(weight, quickItems);
    AddListWeight(weight, spells);
    AddListWeight(weight, armor);
    return weight;
}
