// Item lists: the party's item list and the party unit records' four item lists. See party.h, item.h, unit.h and
// docs/notes/items_spells.md ("Party item lists").
#include "item.h"
#include "party.h"
#include "spell.h"
#include "unit.h"
#include "world.h"

#include <cstdint>

namespace {

// A flag argument read as a byte.
bool ByteFlag(int flag) { return static_cast<std::uint8_t>(flag) != 0; }

// Appends `item` to `list` (SetSize(count + 1) and store, inlined).
void Append(std::vector<CItem*>& list, CItem* item) {
    int index = std::ssize(list);
    list.resize(index + 1);
    list[index] = item;
}

// Removes the element at `index`, closing the gap.
void RemoveAt(std::vector<CItem*>& list, int index) { list.erase(list.begin() + index); }


CItem* AddEntry(std::vector<CItem*>& list, CItem* item) {
    CItem* entry = item->Clone(item->WorldContext()->SpellManager());
    entry->AddRef();
    Append(list, entry);
    return entry;
}

void RemoveEntry(std::vector<CItem*>& list, CItem* entry) {
    for (int i = 0; i < std::ssize(list); ++i) {
        if (list[i] != entry) continue;
        entry->Release();
        RemoveAt(list, i);
        return;
    }
}

// One running sum over the lists, in this order.
void AddListWeight(float& weight, const std::vector<CItem*>& list) {
    for (int i = 0; i < std::ssize(list); ++i) {
        if (list[i]) weight += list[i]->Weight();
    }
}

} // namespace



// --- The party's items --------------------------------------------------------------------------

CItem* CParty::AddItem(CItem* item, int keepCount, int merge) {
    if (!item) return nullptr;
    int count = ByteFlag(keepCount) ? static_cast<int>(item->count) : 1;
    if (ByteFlag(merge)) {
        for (int i = 0; i < std::ssize(items); ++i) {
            CItem* existing = items[i];
            if (!existing->IsSameAs(item)) continue;
            existing->count += static_cast<std::uint32_t>(count);
            return existing;
        }
    }
    CItem* copy = CopyItem(item);
    copy->count = static_cast<std::uint32_t>(count);
    Append(items, copy);
    copy->AddRef();
    return copy;
}

CItem* CParty::RemoveItem(CItem* item, int all) {
    for (int i = 0; i < std::ssize(items); ++i) {
        if (items[i] != item) continue;
        CItem* entry = items[i];
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

void CParty::RemoveQuestItems() {
    for (int i = std::ssize(items) - 1; i >= 0; --i) {
        CItem* item = items[i];
        if (item->GetClassId() == ClassId::ItemQuest) RemoveItem(item, 1);
    }
}

void CParty::ReleaseAllItems() {
    for (int i = 0; i < std::ssize(items); ++i) items[i]->Release();
    items.clear();
}

// --- The party unit records' item lists ---------------------------------------------------------

CItem* PartyUnitRecord::AddWeapon(CItem* item) { return AddEntry(weapons, item); }
CItem* PartyUnitRecord::AddQuickItem(CItem* item) { return AddEntry(quickItems, item); }
CItem* PartyUnitRecord::AddArmor(CItem* item) { return AddEntry(armor, item); }
CItem* PartyUnitRecord::AddSpell(CItem* item) { return AddEntry(spells, item); }

void PartyUnitRecord::RemoveWeapon(CItem* entry) { RemoveEntry(weapons, entry); }
void PartyUnitRecord::RemoveQuickItem(CItem* entry) { RemoveEntry(quickItems, entry); }
void PartyUnitRecord::RemoveArmor(CItem* entry) { RemoveEntry(armor, entry); }
void PartyUnitRecord::RemoveSpell(CItem* entry) { RemoveEntry(spells, entry); }

// --- The weight a unit record carries ---------------------------------------------

float PartyUnitRecord::CarriedWeight() const {
    float weight = 0.0f;
    AddListWeight(weight, weapons);
    AddListWeight(weight, quickItems);
    AddListWeight(weight, spells);
    AddListWeight(weight, armor);
    return weight;
}
