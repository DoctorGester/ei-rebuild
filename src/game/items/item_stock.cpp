// The trader stock generation: see docs/notes/items_spells.md ("Trader stock").
#include "game/items/item_stock.h"

#include "engine/core/core_streams.h"
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "game/spells/spell_db.h"
#include "game/items/item_manager.h"
#include "game/players/party.h"
#include "engine/core/random.h"
#include "game/spells/spell.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/rounding.h"
#include "lib/str.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

// The material draw's window on a material's unit price, as shares of the prototype's price: the price it likes
// best (a seventh) and the upper end of the window (a fifth). The window is symmetric around the best price.
constexpr float kBestUnitPriceShare = 0.14285715f;
constexpr float kWindowEndShare = 0.2f;
// The most modifiers a generated spell takes.
constexpr int kMaxSpellModifiers = 8;

// The count drawn from the CRT generator: max when min >= max, else min + a value below the range
// size (rand scaled by size / 0x7FFF, then modulo the size).
int RandomCount(int minCount, int maxCount) {
    if (minCount >= maxCount) return maxCount;
    int range = maxCount + 1 - minCount;
    return libcrt::RandBelow(range) + minCount;
}

// STLport random_shuffle with the CRT generator: element i swaps with one at rand() % (i + 1).
template <typename T>
void Shuffle(std::vector<T>& v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        int r = libcrt::Rand() % (static_cast<int>(i) + 1);
        std::swap(v[i], v[r]);
    }
}

// Picks the material of a prototype from the candidates (pointers into the stock's entries). Each candidate in
// turn is taken with a chance by its unitPrice q against the prototype's price p: 10% outside
// [2p/7 - p/5, p/5], else 110 - |(q - p/7) / (p/7 - p/5)| x 100 percent (truncated). Null when none was taken.
StockMaterial* DrawMaterial(const itemdb::ItemRecordHead* prototype, const std::vector<StockMaterial*>& candidates) {
    float best = prototype->price * kBestUnitPriceShare;
    float windowEnd = prototype->price * kWindowEndShare;
    if (candidates.empty()) return nullptr;
    float low = (best + best) - windowEnd;
    for (StockMaterial* candidate : candidates) {
        int chance = 10;
        float quality = candidate->record->unitPrice;
        if (quality >= low && quality <= windowEnd) {
            float t = ((quality - best) / (best - windowEnd)) * 100.0f;
            chance = static_cast<int>(110.0f - AbsBits(t));
        }
        int roll = libcrt::RandBelow(100) + 1;
        if (roll < chance) return candidate;
    }
    return nullptr;
}

// The spells generated for an item (held until it is built) and the one it gets (null for none).
struct RolledSpell {
    std::vector<HeldRef<Spell>> generated;
    Spell* chosen = nullptr;
};

// The random spell of a weapon or armor: half the time (ISAAC), when the material allows magic, one of the
// spells generated for the item's level, if within the item's power.
RolledSpell RollItemSpell(ItemStock* stock, ItemKind kind, const itemdb::ItemRecordHead* prototype,
                          const itemdb::MaterialRecordHead* material) {
    RolledSpell roll;
    if (g_random.Next() % 101 >= 50) return roll;
    std::optional<MagicCapacity> capacity = MagicCapacityOf(kind, prototype, material);
    if (!capacity) return roll;
    float levels[kSchoolCount];
    for (float& l : levels) l = capacity->level;
    roll.generated = stock->GenerateSpells(levels, 0);
    unsigned count = static_cast<unsigned>(roll.generated.size());
    if (count == 0) return roll;
    unsigned index = count > 1 ? g_random.Next() % count : 0;
    Spell* spell = roll.generated[index];
    if (spell->manaCost <= capacity->power) roll.chosen = spell;
    return roll;
}

// The table index of each record (none of the item family `skip`; the spell records have no family).
template <typename Record>
std::vector<int> ItemIndices(const std::vector<Record*>& records, const itemdb::RecordTable<Record>& table,
                             const char* skip = nullptr) {
    std::vector<int> indices;
    for (const Record* record : records) {
        if constexpr (requires { record->family; }) {
            if (skip && record->family == skip) continue;
        }
        indices.push_back(table.IndexOf(record));
    }
    return indices;
}

// One instruction item per index: kind ItemKind::Loot with the loot prototype named `prototypeName`, no material,
// index the index and count a random count.
void GenerateInstructions(ItemStock* stock, const char* prototypeName, const std::vector<int>& indices, int minCount,
                          int maxCount, std::vector<Item*>& out) {
    ItemConstructor* ctor = stock->party->GetItemConstructor();
    ItemManager* manager = stock->party->ItemFactory();
    itemdb::LootItemRecord* prototype = g_itemDatabase.loot.Named(prototypeName);
    for (int index : indices) {
        Item* item = ctor->BuildItem(manager, ItemKind::Loot, prototype);
        item->AsLoot()->index = static_cast<std::uint32_t>(index);
        item->count = static_cast<std::uint32_t>(RandomCount(minCount, maxCount));
        item->AddRef();
        out.push_back(item);
    }
}

// `record` appended unless null or already listed.
template <typename T>
void AddUnique(std::vector<T*>& list, T* record) {
    if (record && !lib::Contains(list, record)) list.push_back(record);
}

// A record section: a count, then names lower-cased and looked up in `table`.
template <typename Record>
void LoadRecords(ByteStream* stream, itemdb::RecordTable<Record>& table, std::vector<Record*>& list) {
    for (int count = stream->ReadCount(); count > 0; --count) {
        std::string name = stream->ReadString();
        str::MakeLower(name);
        AddUnique(list, table.Named(name));
    }
}

// A map section: a count, then names (as read) looked up by name.
template <typename Record, typename Map>
void LoadMapped(ByteStream* stream, Map& map, std::vector<Record*>& list) {
    for (int count = stream->ReadCount(); count > 0; --count) {
        AddUnique(list, map[stream->ReadString()]);
    }
}

template <typename Record>
void SaveItemRecords(ByteStream* stream, const std::vector<Record*>& list) {
    stream->WriteCount(list.size());
    for (const itemdb::ItemRecordHead* record : list) stream->WriteString(record->name);
}

constexpr int kScrollTries = 10;

} // namespace


// A failed build (EndCreateItem null) is left out (the original used the null item). The
// item is created before its material is drawn (BuildItem would create it after), so a cancelled item still uses
// up an object id.
template <class Record>
void ItemStock::GenerateItems(std::vector<Record*>* prototypes, int minCount, int maxCount, std::vector<Item*>& out,
                              bool withSpells) {
    ItemConstructor* ctor = party->GetItemConstructor();
    ItemManager* manager = party->ItemFactory();

    int count = RandomCount(minCount, maxCount);
    std::vector<Record*> taken;
    if (prototypes->size() > static_cast<std::size_t>(count)) {
        Shuffle(*prototypes);
        taken.assign(prototypes->begin(), prototypes->begin() + count);
    } else {
        taken = *prototypes;
    }

    std::vector<StockMaterial*> candidates;
    for (Record* prototype : taken) {
        if (prototype->family == itemdb::kWandFamily) continue;
        ctor->SetManager(manager);
        ctor->BeginCreateItem(prototype->Kind());
        ctor->prototype = prototype;
        bool needsMaterial = prototype->HasMaterial();
        candidates.clear();
        if (needsMaterial) {
            for (StockMaterial& e : materials) {
                if (prototype->materialClass != e.record->soundName) continue;
                if (static_cast<int>(prototype->materialUnits) > e.left) continue;
                candidates.push_back(&e);
            }
        }
        StockMaterial* chosen = DrawMaterial(prototype, candidates);
        if (!chosen && needsMaterial) {
            if (candidates.empty()) {
                ctor->Cancel();
                ctor->SetManager(nullptr);
                continue;
            }
            Shuffle(candidates);
            chosen = candidates.front();
        }
        if (chosen) chosen->left -= static_cast<int>(prototype->materialUnits);
        ctor->material = chosen ? chosen->record : nullptr;

        RolledSpell roll;   // its spells released after the build
        if (withSpells) {
            roll = RollItemSpell(this, prototype->Kind(), prototype, ctor->material);
            if (roll.chosen) ctor->spell = roll.chosen;
        }
        Item* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        if (!item) continue;
        item->AddRef();
        if (item->Kind() == ItemKind::Quick && item->GetCategory() == QuickItemCategory::Potion)
            item->count = static_cast<std::uint32_t>(libcrt::RandBelow(30) + 20);
        out.push_back(item);
    }
}

void ItemStock::SetLevels(const float (&base)[kSchoolCount], const float (&scroll)[kSchoolCount]) {
    std::copy_n(base, kSchoolCount, baseLevels);
    std::copy_n(scroll, kSchoolCount, scrollLevels);
}

void ItemStock::GenerateWeapons(int minCount, int maxCount, std::vector<Item*>& out) {
    FillSpellMaps(g_spellDatabase.weaponRecipes);
    GenerateItems(&weapons, minCount, maxCount, out, true);
}
void ItemStock::GenerateArmors(int minCount, int maxCount, std::vector<Item*>& out) {
    FillSpellMaps(g_spellDatabase.armorRecipes);
    GenerateItems(&armors, minCount, maxCount, out, true);
}
void ItemStock::GenerateQuickItems(int minCount, int maxCount, std::vector<Item*>& out) {
    GenerateItems(&quickItems, minCount, maxCount, out, false);
}

void ItemStock::GenerateMaterials(int minCount, int maxCount, std::vector<Item*>& out) {
    ItemConstructor* ctor = party->GetItemConstructor();
    ItemManager* manager = party->ItemFactory();
    int count = RandomCount(minCount, maxCount);
    std::vector<StockMaterial> taken;
    if (materials.size() > static_cast<std::size_t>(count)) {
        Shuffle(materials);
        taken.assign(materials.begin(), materials.begin() + count);
    } else {
        taken = materials;
    }
    itemdb::LootItemRecord* prototype = g_itemDatabase.loot.Named(itemdb::kMaterialType);
    for (const StockMaterial& e : taken) {
        int stackSize = libcrt::RandBelow(e.maxCount - e.minCount + 1) + e.minCount;
        Item* item = ctor->BuildItem(manager, ItemKind::Loot, prototype, e.record, nullptr, stackSize);
        item->AddRef();
        out.push_back(item);
    }
}

void ItemStock::GenerateWeaponInstructions(int minCount, int maxCount, std::vector<Item*>& out) {
    GenerateInstructions(this, "instruction weapon", ItemIndices(weapons, g_itemDatabase.weapons), minCount,
                         maxCount, out);
}

void ItemStock::GenerateArmorInstructions(int minCount, int maxCount, std::vector<Item*>& out) {
    GenerateInstructions(this, "instruction armor", ItemIndices(armors, g_itemDatabase.armors), minCount,
                         maxCount, out);
}

void ItemStock::GenerateQuickItemInstructions(int minCount, int maxCount, std::vector<Item*>& out) {
    GenerateInstructions(this, "instruction quick item",
                         ItemIndices(quickItems, g_itemDatabase.quickItems, itemdb::kScrollFamily), minCount, maxCount,
                         out);
}

void ItemStock::GenerateSpellInstructions(int minCount, int maxCount, std::vector<Item*>& out) {
    GenerateInstructions(this, "prototype", ItemIndices(spells, g_spellDatabase.spells), minCount, maxCount, out);
}

void ItemStock::GenerateModifierInstructions(int minCount, int maxCount, std::vector<Item*>& out) {
    GenerateInstructions(this, "rune", ItemIndices(modifiers, g_spellDatabase.modifiers), minCount, maxCount, out);
}

// Kept bug: the lookup inserts a null entry into the spell map for every recipe name it does not know.
void ItemStock::FillSpellMaps(const std::vector<itemdb::SpellListRecord>& recipes) {
    spellMap.clear();
    modifierMap.clear();
    for (const itemdb::SpellListRecord& recipe : recipes) {
        if (!(recipe.classMask & (1u << (classIndex & 31)))) continue;
        itemdb::SpellRecord* spell = g_spellDatabase.spells.names[recipe.spell];   // keyed by the recipe's spell name
        if (!spell || !lib::Contains(spells, spell)) continue;
        spellMap[recipe.spell].push_back(&recipe);
    }

    for (itemdb::SpellModifierRecord* m : modifiers) {
        switch (m->code[0]) {
        case 'a': case 'd': case 'e': case 'm': case 'r': case 't': {
            itemdb::SpellModifierRecord*& held = modifierMap[std::string(m->code, 1)];
            // Replaced when the held one's strength is less.
            if (!held || held->strength < m->strength) held = m;
            break;
        }
        case 'f': case 'i':
            modifierMap[std::string(m->code, 2)] = m;
            break;
        default:
            break;
        }
    }
}

std::vector<HeldRef<Spell>> ItemStock::GenerateSpells(const float (&levels)[kSchoolCount], unsigned threshold) {
    std::vector<HeldRef<Spell>> made;
    SpellManager* spellManager = party->GetSpellManager();
    std::vector<itemdb::SpellModifierRecord*> chosen;
    for (auto& [spellName, recipes] : spellMap) {
        for (const itemdb::SpellListRecord* recipe : recipes) {
            if (g_random.Next() % 101 < threshold) continue;
            chosen.clear();
            int count = 0;
            itemdb::SpellRecord* prototype = g_spellDatabase.spells.Named(recipe->spell);
            int level = prototype->skill;
            int budget = lib::RoundEven(levels[static_cast<int>(prototype->school)]);
            float cost = prototype->mana;
            for (const std::string& name : recipe->required) {
                itemdb::SpellModifierRecord* m = modifierMap[name];
                if (!m) continue;
                chosen.push_back(m);
                cost += m->mana;
                ++count;
                level += m->skill;
            }
            if (budget < level || count > kMaxSpellModifiers) continue;
            const std::vector<std::string>& optional = recipe->optional;
            for (unsigned i = 0; i < optional.size() && count < kMaxSpellModifiers; ++i) {
                if (g_random.Next() % 101 < 50) continue;
                itemdb::SpellModifierRecord* m = modifierMap[optional[i]];
                if (!m || budget < m->skill + level) continue;
                chosen.push_back(m);
                cost += m->mana;
                ++count;
                level += m->skill;
            }
            itemdb::SpellModifierRecord* repeated = modifierMap[recipe->repeated];
            if (repeated && budget >= repeated->skill + level) {
                int reached = level;   // the repeat loop counts its own level; `level` stays the recipe's
                while (count < kMaxSpellModifiers) {
                    chosen.push_back(repeated);
                    ++count;
                    reached += repeated->skill;
                    if (repeated->code[0] == 'm') {
                        cost = repeated->strength * prototype->mana + cost + repeated->mana;
                        if (cost <= prototype->mana) break;
                    } else {
                        cost += repeated->mana;
                    }
                    if (budget < repeated->skill + reached) break;
                }
            }
            made.emplace_back(spellManager->CreateSpell(prototype, chosen));
        }
    }
    return made;
}

// Original behaviour kept: a material entry is pushed even when its material is not found (null
// record), and the two map lookups insert null entries for unknown names.
void ItemStock::LoadLists(ByteStream* stream) {
    LoadRecords(stream, g_itemDatabase.armors, armors);
    LoadRecords(stream, g_itemDatabase.weapons, weapons);
    LoadRecords(stream, g_itemDatabase.quickItems, quickItems);

    for (int count = stream->ReadCount(); count > 0; --count) {
        std::string name = stream->ReadString();
        str::MakeLower(name);
        const int minCount = stream->ReadAs<std::int32_t>();
        const int maxCount = stream->ReadAs<std::int32_t>();
        const int left = stream->ReadAs<std::int32_t>();
        materials.push_back(StockMaterial{g_itemDatabase.materials.Named(name), minCount, maxCount, left});
    }

    LoadMapped(stream, g_spellDatabase.spells.names, spells);
    LoadMapped(stream, g_spellDatabase.modifiers.names, modifiers);
}

void ItemStock::SaveLists(ByteStream* out) {
    SaveItemRecords(out, armors);
    SaveItemRecords(out, weapons);
    SaveItemRecords(out, quickItems);

    out->WriteCount(materials.size());
    for (const StockMaterial& e : materials) {
        out->WriteString(e.record->key);
        out->WriteValue(e.minCount);
        out->WriteValue(e.maxCount);
        out->WriteValue(e.left);
    }

    out->WriteCount(spells.size());
    for (const itemdb::SpellRecord* p : spells) out->WriteString(p->code);

    // A modifier's code is up to four letters (the original adds an uninitialised stack byte after a four-letter
    // code; here the string stops after the four).
    out->WriteCount(modifiers.size());
    for (const itemdb::SpellModifierRecord* p : modifiers) out->WriteString(std::string(p->code, strnlen(p->code, 4)));
}

void ItemStock::ResetMaterialUnits() {
    for (StockMaterial& e : materials) e.left = RandomCount(e.minCount, e.maxCount);
}

void ItemStock::CopyMagic(const ItemStock& other) {
    spells = other.spells;
    modifiers = other.modifiers;
}

// A failed build (no "spell container" record) is left out (the original used the null item).
void ItemStock::GenerateScrolls(std::vector<Item*>& out) {
    ItemConstructor* ctor = party->GetItemConstructor();
    ItemManager* manager = party->ItemFactory();
    FillSpellMaps(g_spellDatabase.scrollRecipes);
    std::vector<HeldRef<Spell>> list;
    for (int tries = 0; tries < kScrollTries; ++tries) {
        list.clear();   // the last try's spells released before the next are made
        list = GenerateSpells(scrollLevels, 50);
        if (list.size() >= spells.size()) break;
    }
    itemdb::QuickItemRecord* prototype = g_itemDatabase.quickItems.Named(itemdb::kSpellContainer);
    for (Spell* spell : list) {
        Item* item = ctor->BuildItem(manager, ItemKind::Scroll, prototype, nullptr, spell);
        if (!item) continue;
        item->AddRef();
        out.push_back(item);
    }
}

// --- The stock's lists filled by the class template ------------------------------

void ItemStock::AddArmor(itemdb::ArmorRecord* record) { AddUnique(armors, record); }
void ItemStock::AddWeapon(itemdb::WeaponRecord* record) { AddUnique(weapons, record); }
void ItemStock::AddQuickItem(itemdb::QuickItemRecord* record) { AddUnique(quickItems, record); }
void ItemStock::AddSpell(itemdb::SpellRecord* record) { AddUnique(spells, record); }
void ItemStock::AddModifier(itemdb::SpellModifierRecord* record) { AddUnique(modifiers, record); }

void ItemStock::AddMaterial(itemdb::MaterialRecordHead* record, int minCount, int maxCount) {
    if (!record) return;
    auto entry = std::find_if(materials.begin(), materials.end(), [&](const StockMaterial& e) { return e.record == record; });
    if (entry == materials.end()) {
        materials.push_back(StockMaterial{record, minCount, maxCount, maxCount});
        return;
    }
    entry->minCount = minCount;
    entry->maxCount = maxCount;
    entry->left = maxCount;
}
