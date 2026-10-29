// The trader stock generation: see docs/notes/items_spells.md ("Trader stock").
#include "item_stock.h"

#include "core_streams.h"
#include "game_strings.h"
#include "item.h"
#include "item_db.h"
#include "item_manager.h"
#include "party.h"
#include "random.h"
#include "spell.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include "ei/x87.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace {

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
    float seventh = static_cast<float>(prototype->price * stock::kSeventhFactor);
    float fifth = static_cast<float>(prototype->price * stock::kFifthFactor);
    if (candidates.empty()) return nullptr;
    float low = static_cast<float>((seventh + seventh) - fifth);
    for (StockMaterial* candidate : candidates) {
        int chance = 10;
        float quality = candidate->record->unitPrice;
        EI_NAN_AUDIT(fifth);
        if (quality >= low && quality <= fifth) {
            float t = static_cast<float>(((quality - seventh) / (seventh - fifth)) * 100.0f);
            chance = static_cast<int>(110.0f - AbsBits(t));
        }
        int roll = libcrt::RandBelow(100) + 1;
        if (roll < chance) return candidate;
    }
    return nullptr;
}

// The spells generated for an item (held until it is built) and the one it gets (null for none).
struct RolledSpell {
    std::list<HeldRef<CSpell>> generated;
    CSpell* chosen = nullptr;
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
    CSpell* spell = *std::next(roll.generated.begin(), index);
    EI_NAN_AUDIT(spell->manaCost_, capacity->power);
    if (spell->manaCost_ <= capacity->power) roll.chosen = spell;
    return roll;
}

// The table index of each record (none of the family `skip`).
template <typename Record>
std::vector<int> ItemIndices(const std::vector<itemdb::ItemRecordHead*>& records,
                             const itemdb::NamedRecordTable<Record>& table, const char* skip = nullptr) {
    std::vector<int> indices;
    for (const itemdb::ItemRecordHead* record : records) {
        if (skip && record->family == skip) continue;
        indices.push_back(table.IndexOf(static_cast<const Record*>(record)));
    }
    return indices;
}

int SpellIndex(const itemdb::SpellRecord* record) {
    itemdb::SpellTable& spells = g_spellDatabase.spells;
    return spells.IndexOf(spells.FindByCode(record->code));
}
int ModifierIndex(const itemdb::SpellModifierRecord* record) {
    itemdb::SpellModifierTable& modifiers = g_spellDatabase.modifiers;
    return modifiers.IndexOf(modifiers.FindByCode(record->code));
}

template <typename Record>
std::vector<int> MagicIndices(const std::vector<Record*>& records, int (*indexOf)(const Record*)) {
    std::vector<int> indices;
    for (const Record* record : records) indices.push_back(indexOf(record));
    return indices;
}

// One instruction item per index: kind ItemKind::Loot with the loot prototype named `prototypeName`, no material,
// index_ the index and count_ a random count.
void GenerateInstructions(ItemStock* stock, const char* prototypeName, const std::vector<int>& indices, int minCount,
                          int maxCount, std::list<CItem*>* out) {
    CItemConstructor* ctor = stock->party->ItemConstructor();
    CItemManager* manager = stock->party->ItemFactory();
    itemdb::LootItemRecord* prototype = g_itemDatabase.loot.Named(prototypeName);
    for (int index : indices) {
        CItem* item = ctor->BuildItem(manager, ItemKind::Loot, prototype);
        AsLoot(item)->index_ = static_cast<std::uint32_t>(index);
        item->count_ = static_cast<std::uint32_t>(RandomCount(minCount, maxCount));
        item->AddRef();
        out->push_back(item);
    }
}

// `record` appended unless null or already listed.
template <typename T>
void AddUnique(std::vector<T*>& list, T* record) {
    if (record && std::find(list.begin(), list.end(), record) == list.end()) list.push_back(record);
}

// A record section: a count, then names lower-cased and looked up in `table`. `name` and `count` are the
// caller's, reused for every section, so a failed read leaves the previous value.
template <typename Record>
void LoadRecords(CStream* stream, std::string& name, unsigned& count, itemdb::NamedRecordTable<Record>& table,
                 std::vector<itemdb::ItemRecordHead*>& list) {
    stream->ReadValue(count);
    for (unsigned i = 0; i < count; ++i) {
        name = stream->ReadString();
        str::MakeLower(name);
        AddUnique<itemdb::ItemRecordHead>(list, table.Named(name));
    }
}

// A map section: a count, then names (as read) looked up by name.
template <typename Record, typename Map>
void LoadMapped(CStream* stream, std::string& name, unsigned& count, Map& map, std::vector<Record*>& list) {
    stream->ReadValue(count);
    for (unsigned i = 0; i < count; ++i) {
        name = stream->ReadString();
        AddUnique(list, map[name]);
    }
}

void SaveItemRecords(CStream* stream, const std::vector<itemdb::ItemRecordHead*>& list) {
    stream->WriteValue(static_cast<std::uint32_t>(list.size()));
    for (const itemdb::ItemRecordHead* record : list) stream->WriteString(record->name);
}

constexpr int kScrollTries = 10;

} // namespace


// Original bug kept: a failed build (EndCreateItem null) is dereferenced. The item is created before its
// material is drawn (BuildItem would create it after), so a cancelled item still uses up an object id.
void ItemStock::GenerateItems(std::vector<itemdb::ItemRecordHead*>* prototypes, int minCount, int maxCount,
                              std::list<CItem*>* out, const std::vector<itemdb::SpellListRecord>* spellRecipes) {
    CItemConstructor* ctor = party->ItemConstructor();
    CItemManager* manager = party->ItemFactory();
    if (spellRecipes) FillSpellMaps(*spellRecipes);

    int count = RandomCount(minCount, maxCount);
    std::vector<itemdb::ItemRecordHead*> taken;
    if (prototypes->size() > static_cast<std::size_t>(count)) {
        Shuffle(*prototypes);
        taken.assign(prototypes->begin(), prototypes->begin() + count);
    } else {
        taken = *prototypes;
    }

    std::vector<StockMaterial*> candidates;
    for (itemdb::ItemRecordHead* prototype : taken) {
        if (prototype->family == game::WandText()) continue;
        ctor->SetManager(manager);
        ctor->BeginCreateItem(prototype->Kind());
        ctor->prototype_ = prototype;
        bool needsMaterial = text::CompareIgnoringCase(prototype->materialClass.c_str(), game::NoneText()) != 0;
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
        ctor->material_ = chosen ? chosen->record : nullptr;

        RolledSpell roll;   // its spells released after the build
        if (spellRecipes) {
            roll = RollItemSpell(this, prototype->Kind(), prototype, ctor->material_);
            if (roll.chosen) ctor->spell_ = roll.chosen;
        }
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        item->AddRef();
        if (item->Kind() == ItemKind::Quick && item->GetCategory() == QuickItemCategory::Potion)
            item->count_ = static_cast<std::uint32_t>(libcrt::RandBelow(30) + 20);
        out->push_back(item);
    }
}

void ItemStock::SetLevels(const float* base, const float* scroll) {
    std::copy_n(base, kSchoolCount, baseLevels);
    std::copy_n(scroll, kSchoolCount, scrollLevels);
}

void ItemStock::GenerateWeapons(int minCount, int maxCount, std::list<CItem*>* out) {
    GenerateItems(&weapons, minCount, maxCount, out, &g_spellDatabase.weaponRecipes);
}
void ItemStock::GenerateArmors(int minCount, int maxCount, std::list<CItem*>* out) {
    GenerateItems(&armors, minCount, maxCount, out, &g_spellDatabase.armorRecipes);
}
void ItemStock::GenerateQuickItems(int minCount, int maxCount, std::list<CItem*>* out) {
    GenerateItems(&quickItems, minCount, maxCount, out, nullptr);
}

void ItemStock::GenerateMaterials(int minCount, int maxCount, std::list<CItem*>* out) {
    CItemConstructor* ctor = party->ItemConstructor();
    CItemManager* manager = party->ItemFactory();
    int count = RandomCount(minCount, maxCount);
    std::vector<StockMaterial> taken;
    if (materials.size() > static_cast<std::size_t>(count)) {
        Shuffle(materials);
        taken.assign(materials.begin(), materials.begin() + count);
    } else {
        taken = materials;
    }
    itemdb::LootItemRecord* prototype = g_itemDatabase.loot.Named(game::MaterialText());
    for (const StockMaterial& e : taken) {
        int stackSize = libcrt::RandBelow(e.maxCount - e.minCount + 1) + e.minCount;
        CItem* item = ctor->BuildItem(manager, ItemKind::Loot, prototype, e.record, nullptr, stackSize);
        item->AddRef();
        out->push_back(item);
    }
}

void ItemStock::GenerateWeaponInstructions(int minCount, int maxCount, std::list<CItem*>* out) {
    GenerateInstructions(this, game::InstructionWeaponText(), ItemIndices(weapons, g_itemDatabase.weapons), minCount,
                         maxCount, out);
}

void ItemStock::GenerateArmorInstructions(int minCount, int maxCount, std::list<CItem*>* out) {
    GenerateInstructions(this, game::InstructionArmorText(), ItemIndices(armors, g_itemDatabase.armors), minCount,
                         maxCount, out);
}

void ItemStock::GenerateQuickItemInstructions(int minCount, int maxCount, std::list<CItem*>* out) {
    GenerateInstructions(this, game::InstructionQuickItemText(),
                         ItemIndices(quickItems, g_itemDatabase.quickItems, game::ScrollText()), minCount, maxCount,
                         out);
}

void ItemStock::GenerateSpellInstructions(int minCount, int maxCount, std::list<CItem*>* out) {
    GenerateInstructions(this, game::PrototypeText(), MagicIndices(spells, SpellIndex), minCount, maxCount, out);
}

void ItemStock::GenerateModifierInstructions(int minCount, int maxCount, std::list<CItem*>* out) {
    GenerateInstructions(this, game::RuneText(), MagicIndices(modifiers, ModifierIndex), minCount, maxCount, out);
}

// Kept bug: the lookup inserts a null entry into the spell map for every recipe name it does not know.
void ItemStock::FillSpellMaps(const std::vector<itemdb::SpellListRecord>& recipes) {
    spellMap.clear();
    modifierMap.clear();
    for (const itemdb::SpellListRecord& recipe : recipes) {
        if (!(recipe.classMask & (1u << (classIndex & 31)))) continue;
        itemdb::SpellRecord* spell = g_spellDatabase.spells.byCode[recipe.spell];   // keyed by the recipe's spell name
        if (!spell || std::find(spells.begin(), spells.end(), spell) == spells.end()) continue;
        spellMap[recipe.spell].push_back(&recipe);
    }

    for (itemdb::SpellModifierRecord* m : modifiers) {
        switch (m->code[0]) {
        case 'a': case 'd': case 'e': case 'm': case 'r': case 't': {
            itemdb::SpellModifierRecord*& held = modifierMap[std::string(m->code, 1)];
            // Replaced when the held one's strength is less.
            if (held) EI_NAN_AUDIT(held->strength, m->strength);
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

std::list<HeldRef<CSpell>> ItemStock::GenerateSpells(const float* levels, unsigned threshold) {
    using stock::kMaxSpellModifiers;
    std::list<HeldRef<CSpell>> made;
    CSpellManager* spellManager = party->SpellManager();
    std::list<itemdb::SpellModifierRecord*> chosen;
    for (auto& [spellName, recipes] : spellMap) {
        for (const itemdb::SpellListRecord* recipe : recipes) {
            if (g_random.Next() % 101 < threshold) continue;
            chosen.clear();
            int count = 0;
            itemdb::SpellRecord* prototype = g_spellDatabase.spells.Named(recipe->spell);
            int level = prototype->skill;
            int budget = x87::fistp(levels[prototype->school]);
            float cost = prototype->mana;
            for (const std::string& name : recipe->required) {
                itemdb::SpellModifierRecord* m = modifierMap[name];
                if (!m) continue;
                chosen.push_back(m);
                cost = static_cast<float>(cost + m->mana);
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
                cost = static_cast<float>(cost + m->mana);
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
                        cost = static_cast<float>(repeated->strength * prototype->mana + cost + repeated->mana);
                        EI_NAN_AUDIT(cost, prototype->mana);
                        if (cost <= prototype->mana) break;
                    } else {
                        cost = static_cast<float>(cost + repeated->mana);
                    }
                    if (budget < repeated->skill + reached) break;
                }
            }
            made.emplace_back(spellManager->CreateSpell(prototype, &chosen));
        }
    }
    return made;
}

// Original behaviour kept: a material entry is pushed even when its material is not found (null
// record), and the two map lookups insert null entries for unknown names.
void ItemStock::LoadLists(CStream* stream) {
    std::string name;
    unsigned count = 0;
    itemdb::ItemDatabase& items = g_itemDatabase;
    LoadRecords(stream, name, count, items.armors, armors);
    LoadRecords(stream, name, count, items.weapons, weapons);
    LoadRecords(stream, name, count, items.quickItems, quickItems);

    // The three ints are reused for every entry, like the counts.
    stream->ReadValue(count);
    int values[3] = {0, 0, 0};
    for (unsigned i = 0; i < count; ++i) {
        name = stream->ReadString();
        str::MakeLower(name);
        for (int& v : values) stream->ReadValue(v);
        materials.push_back(StockMaterial{items.materials.Named(name), values[0], values[1], values[2]});
    }

    LoadMapped(stream, name, count, g_spellDatabase.spells.byCode, spells);
    LoadMapped(stream, name, count, g_spellDatabase.modifiers.byCode, modifiers);
}

void ItemStock::SaveLists(CStream* out) {
    SaveItemRecords(out, armors);
    SaveItemRecords(out, weapons);
    SaveItemRecords(out, quickItems);

    out->WriteValue(static_cast<std::uint32_t>(materials.size()));
    for (const StockMaterial& e : materials) {
        out->WriteString(e.record->key);
        out->WriteValue(static_cast<std::uint32_t>(e.minCount));
        out->WriteValue(static_cast<std::uint32_t>(e.maxCount));
        out->WriteValue(static_cast<std::uint32_t>(e.left));
    }

    out->WriteValue(static_cast<std::uint32_t>(spells.size()));
    for (const itemdb::SpellRecord* p : spells) out->WriteString(p->code);

    // A modifier's code is up to four letters (the original adds an uninitialised stack byte after a four-letter
    // code; here the string stops after the four).
    out->WriteValue(static_cast<std::uint32_t>(modifiers.size()));
    for (const itemdb::SpellModifierRecord* p : modifiers) out->WriteString(std::string(p->code, strnlen(p->code, 4)));
}

void ItemStock::ResetMaterialUnits() {
    for (StockMaterial& e : materials) e.left = RandomCount(e.minCount, e.maxCount);
}

void ItemStock::CopyMagic(const ItemStock* other) {
    spells = other->spells;
    modifiers = other->modifiers;
}

// Original bug kept: a failed build (null item) is dereferenced.
void ItemStock::GenerateScrolls(std::list<CItem*>* out) {
    CItemConstructor* ctor = party->ItemConstructor();
    CItemManager* manager = party->ItemFactory();
    FillSpellMaps(g_spellDatabase.scrollRecipes);
    std::list<HeldRef<CSpell>> list;
    for (int tries = 0; tries < kScrollTries; ++tries) {
        list.clear();   // the last try's spells released before the next are made
        list = GenerateSpells(scrollLevels, 50);
        if (list.size() >= spells.size()) break;
    }
    itemdb::QuickItemRecord* prototype = g_itemDatabase.quickItems.Named(game::SpellContainerText());
    for (CSpell* spell : list) {
        CItem* item = ctor->BuildItem(manager, ItemKind::Scroll, prototype, nullptr, spell);
        item->AddRef();
        out->push_back(item);
    }
}

// --- The stock's lists filled by the class template ------------------------------

void ItemStock::AddArmor(itemdb::ArmorRecord* record) { AddUnique<itemdb::ItemRecordHead>(armors, record); }
void ItemStock::AddWeapon(itemdb::WeaponRecord* record) { AddUnique<itemdb::ItemRecordHead>(weapons, record); }
void ItemStock::AddQuickItem(itemdb::QuickItemRecord* record) { AddUnique<itemdb::ItemRecordHead>(quickItems, record); }
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
