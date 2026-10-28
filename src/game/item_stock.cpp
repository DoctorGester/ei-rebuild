// The trader stock generation: see docs/notes/items_spells.md ("Trader stock").
#include "item_stock.h"

#include "core_streams.h"
#include "game_strings.h"
#include "item.h"
#include "item_db.h"
#include "party.h"
#include "random.h"
#include "spell.h"

#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "../lib/stlp.h"
#include "../lib/text_case.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/containers.h"
#include "lib/static_object.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {


using stock::CandidateVector;
using stock::Candidates;
using stock::TakenMaterials;
using stock::TakenPrototypes;


// The template's party object (ItemStock::context): its item constructor, item manager and spell manager.
CParty* PartyOf(const ItemStock* stock) { return stock->context; }
CItemConstructor* ConstructorOf(const ItemStock* stock) { return PartyOf(stock)->ItemConstructor(); }

// The count drawn from the CRT generator: max when min >= max, else min + a value below the range
// size (rand scaled by size / 0x7FFF, then modulo the size).
int RandomCount(int minCount, int maxCount) {
    if (minCount >= maxCount) return maxCount;
    int range = maxCount + 1 - minCount;
    return gamecrt::rand() * range / 0x7FFF % range + minCount;
}

// STLport random_shuffle with the CRT generator: element i swaps with one at rand() % (i + 1).
template <typename T>
void Shuffle(T* first, T* last) {
    if (first == last) return;
    for (T* i = first + 1; i != last; ++i) {
        int r = gamecrt::rand() % (static_cast<int>(i - first) + 1);
        T held = *i;
        *i = first[r];
        first[r] = held;
    }
}

// The callers skip a self-assignment.

// Counted by the caller.
void Output(StockList* out, CItem* item) { out->insert(out->end(), item); }

void AddReference(CItem* item) { ++item->refCount_; }

const char* PrototypeName(const itemdb::ItemRecordHead* p) { return p->text08.c_str(); }
const char* MaterialClass(const itemdb::ItemRecordHead* p) { return p->materialClass.c_str(); }
int MaterialUnits(const itemdb::ItemRecordHead* p) { return static_cast<int>(p->materialUnits); }

// Picks the material of a prototype from the candidates (pointers into the stock's entries). Each candidate in
// turn is taken with a chance by its unitPrice q against the prototype's price p: 10% outside
// [2p/7 - p/5, p/5], else 110 - |(q - p/7) / (p/7 - p/5)| x 100 percent (truncated). Null when none was taken.
StockMaterial* DrawMaterial(const itemdb::ItemRecordHead* prototype) {
    float seventh = static_cast<float>(prototype->price * stock::SeventhFactor());
    float fifth = static_cast<float>(prototype->price * stock::FifthFactor());
    CandidateVector& c = Candidates();
    if (c.begin() == c.end()) return nullptr;
    float low = static_cast<float>((seventh + seventh) - fifth);
    for (StockMaterial** q = c.data(); q != c.data() + c.size(); ++q) {
        int chance = 10;
        float quality = (*q)->record->unitPrice;
        EI_NAN_AUDIT(fifth);
        if (quality >= low && quality <= fifth) {
            float t = static_cast<float>(((quality - seventh) / (seventh - fifth)) * 100.0f);
            std::uint32_t bits;
            std::memcpy(&bits, &t, 4);
            bits &= 0x7FFFFFFFu;
            std::memcpy(&t, &bits, 4);
            chance = static_cast<int>(110.0f - t);
        }
        int roll = gamecrt::rand() * 100 / 0x7FFF % 100 + 1;
        if (roll < chance) return *q;
    }
    return nullptr;
}

// The random spell of a weapon or armor: half the time, when the material allows magic, one of the spells
// generated for the item's level, if within the power limit.
void AddSpell(ItemStock* stock, CItemConstructor* ctor, StockSpellList& spells) {
    if (IsaacRandom::Global().Next() % 101 >= 50) return;
    float level = 0.0f;
    float power = 0.0f;
    if (!ctor->MagicCapacity(&level, &power)) return;
    float levels[8];
    for (float& l : levels) l = level;
    stock->GenerateSpells(&spells, levels, 0);
    unsigned count = static_cast<unsigned>(spells.size());
    if (count == 0) return;
    unsigned index = count > 1 ? IsaacRandom::Global().Next() % count : 0;
    CSpell* spell = *std::next(spells.begin(), index);
    float spellPower = spell->manaCost_;
    EI_NAN_AUDIT(spellPower, power);
    if (spellPower <= power) ctor->SetSpellObject(spell);
}

} // namespace


// Original bug kept: a failed build (EndCreateItem null) is dereferenced.
void ItemStock::GenerateItems(StockPrototypeVector* prototypes, int minCount, int maxCount, StockList* out, int kind) {
    CItemConstructor* ctor = ConstructorOf(this);
    CItemManager* manager = PartyOf(this)->ItemManager();
    if (kind == 0 || kind == 3)
        FillSpellMaps(stock::WeaponSpellRecipes());
    else if (kind == 1)
        FillSpellMaps(stock::ArmorSpellRecipes());

    int count = RandomCount(minCount, maxCount);
    StockPrototypeVector& taken = TakenPrototypes();
    taken.clear();   // erase(begin, end)
    if (prototypes->size() > static_cast<std::size_t>(count)) {
        Shuffle(prototypes->data(), prototypes->data() + prototypes->size());
        taken.insert(taken.begin(), prototypes->begin(), prototypes->begin() + count);
    } else if (prototypes != &taken) {
        taken = *prototypes;
    }

    CandidateVector& candidates = Candidates();
    for (itemdb::ItemRecordHead** p = taken.data(); p != taken.data() + taken.size(); ++p) {
        itemdb::ItemRecordHead* prototype = *p;
        if (str::Compare(PrototypeName(prototype), game::WandText()) == 0) continue;
        ctor->SetManager(manager);
        ctor->BeginCreateItem(prototype->Kind(), -1);
        ctor->SetPrototype(prototype);
        candidates.clear();   // erase(begin, end)
        if (text::CompareIgnoringCase(MaterialClass(prototype), game::NoneText()) != 0) {
            for (StockMaterial* e = materials.data(); e != materials.data() + materials.size(); ++e) {
                if (str::Compare(MaterialClass(prototype), e->record->soundName) != 0) continue;
                if (MaterialUnits(prototype) > e->left) continue;
                candidates.push_back(e);
            }
        }
        StockMaterial* chosen = DrawMaterial(prototype);
        if (!chosen) {
            if (text::CompareIgnoringCase(MaterialClass(prototype), game::NoneText()) != 0) {
                if (candidates.end() == candidates.begin()) {
                    ctor->Cancel();
                    ctor->SetManager(nullptr);
                    continue;
                }
                Shuffle(candidates.data(), candidates.data() + candidates.size());
                chosen = *candidates.begin();
            }
        }
        if (chosen) {
            chosen->left -= MaterialUnits(prototype);
            ctor->SetMaterial(chosen->record);
        } else {
            ctor->SetMaterial(nullptr);
        }

        StockSpellList spellList;
        if (kind == 1 || kind == 3 || kind == 0) ::AddSpell(this, ctor, spellList);
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        AddReference(item);
        if (item->type_ == 0x3006 && item->GetCategory() == 8)
            item->count_ = static_cast<std::uint32_t>(gamecrt::rand() * 30 / 0x7FFF % 30 + 20);
        Output(out, item);
    }
    candidates.clear();
}

void ItemStock::SetLevels(const float* base, const float* scroll) {
    for (int i = 0; i < 8; ++i) baseLevels[i] = base[i];
    for (int i = 0; i < 8; ++i) scrollLevels[i] = scroll[i];
}

void ItemStock::GenerateWeapons(int minCount, int maxCount, StockList* out) { GenerateItems(&weapons, minCount, maxCount, out, 0); }
void ItemStock::GenerateArmors(int minCount, int maxCount, StockList* out) { GenerateItems(&armors, minCount, maxCount, out, 1); }
void ItemStock::GenerateQuickItems(int minCount, int maxCount, StockList* out) { GenerateItems(&quickItems, minCount, maxCount, out, 2); }

void ItemStock::GenerateMaterials(int minCount, int maxCount, StockList* out) {
    CItemConstructor* ctor = ConstructorOf(this);
    CItemManager* manager = PartyOf(this)->ItemManager();
    int count = RandomCount(minCount, maxCount);
    StockMaterialVector& taken = TakenMaterials();
    taken.clear();   // erase(begin, end)
    if (materials.size() > static_cast<std::size_t>(count)) {
        Shuffle(materials.data(), materials.data() + materials.size());
        taken.insert(taken.begin(), materials.begin(), materials.begin() + count);
    } else if (&materials != &taken) {
        taken = materials;
    }
    itemdb::LootItemRecord* prototype = itemdb::TheItemDatabase().loot.Named(game::MaterialText());
    for (StockMaterial* e = taken.data(); e != taken.data() + taken.size(); ++e) {
        ctor->SetManager(manager);
        ctor->BeginCreateItem(0x3007, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(e->record);
        int range = e->maxCount - e->minCount + 1;
        ctor->count_ = gamecrt::rand() * range / 0x7FFF % range + e->minCount;
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        AddReference(item);
        Output(out, item);
    }
    taken.clear();
}

namespace {

// The common body of the three instruction generators: the source records are copied to a scratch vector, then
// each (except one named `skip`) becomes an item of kind 0x3007 with the class-0x15 prototype named at
// `prototypeName`, no material, index_ the record's index in `table` and count_ a random count.
template <typename Record>
void GenerateInstructions(ItemStock* stock, const StockPrototypeVector& source, const char* prototypeName,
                          const itemdb::NamedRecordTable<Record>& table, const char* skip, int minCount, int maxCount,
                          StockList* out) {
    CItemConstructor* ctor = ConstructorOf(stock);
    CItemManager* manager = PartyOf(stock)->ItemManager();
    StockPrototypeVector& taken = TakenPrototypes();
    taken.clear();   // erase(begin, end)
    if (&source != &taken) taken = source;
    itemdb::LootItemRecord* prototype = itemdb::TheItemDatabase().loot.Named(prototypeName);
    for (itemdb::ItemRecordHead** p = taken.data(); p != taken.data() + taken.size(); ++p) {
        if (skip && str::Compare(PrototypeName(*p), skip) == 0) continue;
        ctor->SetManager(manager);
        ctor->BeginCreateItem(0x3007, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(nullptr);
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        AsLoot(item)->index_ = static_cast<std::uint32_t>(table.IndexOf(static_cast<const Record*>(*p)));
        item->count_ = static_cast<std::uint32_t>(RandomCount(minCount, maxCount));
        AddReference(item);
        Output(out, item);
    }
    taken.clear();
}

} // namespace

void ItemStock::GenerateWeaponInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, weapons, game::InstructionWeaponText(), itemdb::TheItemDatabase().weapons, nullptr, minCount,
                         maxCount, out);
}

void ItemStock::GenerateArmorInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, armors, game::InstructionArmorText(), itemdb::TheItemDatabase().armors, nullptr, minCount,
                         maxCount, out);
}

// Quick items named "scroll" get no instruction.
void ItemStock::GenerateQuickItemInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, quickItems, game::InstructionQuickItemText(), itemdb::TheItemDatabase().quickItems,
                         game::ScrollText(), minCount, maxCount, out);
}

namespace {

itemdb::SpellRecord* FindSpellPrototype(const char* name) {
    return itemdb::TheSpellDatabase().spells.Named(name);
}

// Created null when missing.
itemdb::SpellModifierRecord*& ModifierSlot(ItemStock* stock, const std::string& name) {
    return stock->modifierMap[name];
}

using Modifier = itemdb::SpellModifierRecord;
char ModifierKind(const Modifier* m) { return m->code[0]; }
float ModifierCost(const Modifier* m) { return m->mana; }
float ModifierFactor(const Modifier* m) { return m->strength; }
int ModifierLevel(const Modifier* m) { return m->skill; }

void AppendModifier(SpellModifierList& list, Modifier* modifier) { list.insert(list.end(), modifier); }

void ClearModifierList(SpellModifierList& list) { list.clear(); }

} // namespace

// Kept bug: the lookup inserts a null entry into the spell map for every recipe name it does not know.
void ItemStock::FillSpellMaps(const SpellRecipeVector* recipes) {
    spellMap.clear();
    modifierMap.clear();
    for (const SpellRecipe& listed : *recipes) {
        const SpellRecipe* recipe = &listed;
        if (!(recipe->classMask & (1u << (classIndex & 31)))) continue;
        itemdb::SpellRecord* spell = stock::SpellsByName()[recipe->spell];   // keyed by the recipe's spell name
        if (!spell) continue;
        bool offered = false;
        for (itemdb::SpellRecord** s = spells.data(); s != spells.data() + spells.size(); ++s) {
            if (*s == spell) {
                offered = true;
                break;
            }
        }
        if (!offered) continue;
        StockRecipeList* entries = &spellMap[recipe->spell];
        entries->insert(entries->end(), const_cast<SpellRecipe*>(recipe));   // the recipe lists hold raw record pointers
    }

    for (Modifier** p = modifiers.data(); p != modifiers.data() + modifiers.size(); ++p) {
        Modifier* m = *p;
        char letters[2] = {m->code[0], m->code[1]};
        switch (ModifierKind(m)) {
        case 'a': case 'd': case 'e': case 'm': case 'r': case 't': {
            std::string key(letters, letters + 1);
            Modifier* held = ModifierSlot(this, key);
            // Replaced when the held one's strength is less.
            if (held) EI_NAN_AUDIT(ModifierFactor(held), ModifierFactor(m));
            if (!held || ModifierFactor(held) < ModifierFactor(m)) ModifierSlot(this, key) = m;
            break;
        }
        case 'f': case 'i': {
            std::string key(letters, letters + 2);
            ModifierSlot(this, key) = m;
            break;
        }
        default:
            break;
        }
    }
}

void ItemStock::GenerateSpells(StockSpellList* out, const float* levels, unsigned threshold) {
    CSpellManager* spellManager = PartyOf(this)->SpellManager();
    SpellModifierList chosen;
    for (auto& [spellName, recipes] : spellMap) {
        for (void* entry : recipes) {
            if (IsaacRandom::Global().Next() % 101 < threshold) continue;
            ClearModifierList(chosen);
            int count = 0;
            auto* recipe = static_cast<const SpellRecipe*>(entry);
            itemdb::SpellRecord* prototype = FindSpellPrototype(recipe->spell.data());
            int level = prototype->skill;
            int budget = x87::fistp(levels[prototype->school]);
            float cost = prototype->mana;
            const StockStringVector& required = recipe->required;
            for (unsigned i = 0; i < required.size(); ++i) {
                Modifier* m = ModifierSlot(this, required[i]);
                if (!m) continue;
                AppendModifier(chosen, m);
                cost = static_cast<float>(cost + ModifierCost(m));
                ++count;
                level += ModifierLevel(m);
            }
            if (budget < level || count > 8) continue;
            const StockStringVector& optional = recipe->optional;
            for (unsigned i = 0; i < optional.size() && count < 8; ++i) {
                if (IsaacRandom::Global().Next() % 101 < 50) continue;
                Modifier* m = ModifierSlot(this, optional[i]);
                if (!m || budget < ModifierLevel(m) + level) continue;
                AppendModifier(chosen, m);
                cost = static_cast<float>(cost + ModifierCost(m));
                ++count;
                level += ModifierLevel(m);
            }
            Modifier* repeated = ModifierSlot(this, recipe->repeated);
            if (repeated && budget >= ModifierLevel(repeated) + level) {
                int reached = level;   // (kept in a register; `level` is not updated)
                while (count < 8) {
                    AppendModifier(chosen, repeated);
                    ++count;
                    reached += ModifierLevel(repeated);
                    if (ModifierKind(repeated) == 'm') {
                        cost = static_cast<float>(ModifierFactor(repeated) * prototype->mana + cost + ModifierCost(repeated));
                        EI_NAN_AUDIT(cost, prototype->mana);
                        if (cost <= prototype->mana) break;
                    } else {
                        cost = static_cast<float>(cost + ModifierCost(repeated));
                    }
                    if (budget < ModifierLevel(repeated) + reached) break;
                }
            }
            out->emplace_back(spellManager->CreateSpell(prototype, &chosen));
        }
    }
}

namespace {

// The counts and the material values are read into locals reused for every section, so a failed read leaves the
// previous value.
void ReadInt(CStream* stream, void* value) { stream->Read(value, 4u); }

void ReadName(CStream* stream, std::string& name) {
    std::string temporary;
    std::string* read = stream->ReadString(&temporary);
    name = *read;
}

template <typename T>
void AddUnique(std::vector<T*>& list, T* record) {
    if (std::find(list.begin(), list.end(), record) == list.end()) list.push_back(record);
}

// A record section: a count, then names lower-cased and looked up in `table`.
template <typename Record>
void LoadRecords(CStream* stream, std::string& name, unsigned& count, itemdb::NamedRecordTable<Record>& table,
                 StockPrototypeVector& list) {
    ReadInt(stream, &count);
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        str::MakeLower(name);
        itemdb::ItemRecordHead* record = table.Named(name.c_str());
        if (record) AddUnique(list, record);
    }
}

// A map section: a count, then names (as read) looked up by name.
template <typename Record, typename Map>
void LoadMapped(CStream* stream, std::string& name, unsigned& count, Map& map, std::vector<Record*>& list) {
    ReadInt(stream, &count);
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        Record* record;
        {
            std::string key(name.c_str());
            record = map[key];
        }
        if (record) AddUnique(list, record);
    }
}

} // namespace

// Original behaviour kept: a material entry is pushed even when its material is not found (null
// record), and the two map lookups insert null entries for unknown names.
void ItemStock::LoadLists(CStream* stream) {
    PartyOf(this)->SpellManager();   // (the spell and item managers, fetched and not used)
    PartyOf(this)->ItemManager();
    std::string name;
    unsigned count = 0;
    itemdb::ItemDatabase& items = itemdb::TheItemDatabase();
    LoadRecords(stream, name, count, items.armors, armors);
    LoadRecords(stream, name, count, items.weapons, weapons);
    LoadRecords(stream, name, count, items.quickItems, quickItems);

    ReadInt(stream, &count);
    int values[3] = {0, 0, 0};
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        str::MakeLower(name);
        StockMaterial entry;
        for (int& v : values) ReadInt(stream, &v);
        entry.record = items.materials.Named(name.c_str());
        entry.minCount = values[0];
        entry.maxCount = values[1];
        entry.left = values[2];
        materials.push_back(entry);
    }

    LoadMapped(stream, name, count, stock::SpellsByName(), spells);
    LoadMapped(stream, name, count, stock::ModifiersByName(), modifiers);
}

namespace {

void WriteInt(CStream* stream, std::uint32_t value) { stream->Write(&value, 4u); }

void SaveItemRecords(CStream* stream, const StockPrototypeVector& list) {
    WriteInt(stream, static_cast<std::uint32_t>(list.size()));
    for (const itemdb::ItemRecordHead* record : list) stream->WriteString(record->text04);
}

} // namespace

void ItemStock::SaveLists(CStream* out) {
    SaveItemRecords(out, armors);
    SaveItemRecords(out, weapons);
    SaveItemRecords(out, quickItems);

    WriteInt(out, static_cast<std::uint32_t>(materials.size()));
    for (StockMaterial* e = materials.data(); e != materials.data() + materials.size(); ++e) {
        out->WriteString(e->record->key);
        WriteInt(out, static_cast<std::uint32_t>(e->minCount));
        WriteInt(out, static_cast<std::uint32_t>(e->maxCount));
        WriteInt(out, static_cast<std::uint32_t>(e->left));
    }

    WriteInt(out, static_cast<std::uint32_t>(spells.size()));
    for (itemdb::SpellRecord** p = spells.data(); p != spells.data() + spells.size(); ++p) out->WriteString((*p)->code);

    // The modifiers' codes go through a CString made from a stack copy of their first four bytes (the game's
    // fifth byte is an uninitialised stack byte, its sixth a zero; here the string stops after the four).
    WriteInt(out, static_cast<std::uint32_t>(modifiers.size()));
    for (Modifier** p = modifiers.data(); p != modifiers.data() + modifiers.size(); ++p) {
        char code[5] = {};
        std::memcpy(code, (*p)->code, 4);
        std::string text = code;
        out->WriteString(text);
    }
}

void ItemStock::ResetMaterialUnits() {
    for (StockMaterial* e = materials.data(); e != materials.data() + materials.size(); ++e) {
        e->left = e->maxCount;
        if (e->maxCount > e->minCount) e->left = RandomCount(e->minCount, e->maxCount);
    }
}

void ItemStock::CopyMagic(const ItemStock* other) {
    if (&other->spells != &spells) spells = other->spells;
    if (&other->modifiers != &modifiers) modifiers = other->modifiers;
}

namespace {

int SpellIndex(const itemdb::SpellRecord* record) {
    const char* code = record->code.c_str();
    itemdb::SpellTable& spells = itemdb::TheSpellDatabase().spells;
    return spells.IndexOf(spells.FindByCode(code));
}
int ModifierIndex(const Modifier* record) {
    const char* code = record->code;
    itemdb::SpellModifierTable& modifiers = itemdb::TheSpellDatabase().modifiers;
    return modifiers.IndexOf(modifiers.FindByCode(code));
}

// The common body of the spell and modifier instruction generators: the source copied to its scratch vector,
// then an item of kind 0x3007 with the class-0x15 prototype named at `prototypeName` per record, its index,
// count_ a random count. The scratch is emptied at the end.
template <typename Record>
void GenerateMagicInstructions(ItemStock* stock, const std::vector<Record*>& source, std::vector<Record*>& taken,
                               const char* prototypeName, int (*indexOf)(const Record*), int minCount, int maxCount,
                               StockList* out) {
    CItemConstructor* ctor = ConstructorOf(stock);
    CItemManager* manager = PartyOf(stock)->ItemManager();
    PartyOf(stock)->SpellManager();   // (fetched and not used)
    taken.clear();
    if (&source != &taken) taken = source;
    itemdb::LootItemRecord* prototype = itemdb::TheItemDatabase().loot.Named(prototypeName);
    for (Record* record : taken) {
        ctor->SetManager(manager);
        ctor->BeginCreateItem(0x3007, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(nullptr);
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        AsLoot(item)->index_ = static_cast<std::uint32_t>(indexOf(record));
        item->count_ = static_cast<std::uint32_t>(RandomCount(minCount, maxCount));
        AddReference(item);
        Output(out, item);
    }
    taken.clear();
}

} // namespace

void ItemStock::GenerateSpellInstructions(int minCount, int maxCount, StockList* out) {
    GenerateMagicInstructions(this, spells, stock::TakenSpells(), game::PrototypeText(), SpellIndex, minCount, maxCount, out);
}

void ItemStock::GenerateModifierInstructions(int minCount, int maxCount, StockList* out) {
    GenerateMagicInstructions(this, modifiers, stock::TakenModifiers(), game::RuneText(), ModifierIndex, minCount, maxCount, out);
}

namespace {
constexpr int kScrollTries = 10;
} // namespace

// Original bug kept: a failed build (EndCreateItem null) is dereferenced.
void ItemStock::GenerateScrolls(int /*minCount*/, int /*maxCount*/, StockList* out) {
    CItemConstructor* ctor = ConstructorOf(this);
    CItemManager* manager = PartyOf(this)->ItemManager();
    PartyOf(this)->SpellManager();   // (fetched and not used)
    FillSpellMaps(stock::ScrollSpellRecipes());
    StockSpellList list;
    for (int tries = 0;;) {
        list.clear();
        GenerateSpells(&list, scrollLevels, 0x32);
        if (list.size() >= spells.size()) break;
        if (++tries >= kScrollTries) break;
    }
    itemdb::QuickItemRecord* prototype = itemdb::TheItemDatabase().quickItems.Named(game::SpellContainerText());
    for (CSpell* spell : list) {
        ctor->SetManager(manager);
        ctor->BeginCreateItem(0x3008, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(nullptr);
        ctor->SetSpellObject(spell);
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        AddReference(item);
        Output(out, item);
    }
}

// --- The stock's lists filled by the class template ------------------------------

namespace {

template <typename T>
void AddUnlessPresent(std::vector<T*>& list, T* record) {
    if (!record) return;
    if (std::find(list.begin(), list.end(), record) == list.end()) list.push_back(record);
}

} // namespace

void ItemStock::AddArmor(itemdb::ArmorRecord* record) { AddUnlessPresent<itemdb::ItemRecordHead>(armors, record); }
void ItemStock::AddWeapon(itemdb::WeaponRecord* record) { AddUnlessPresent<itemdb::ItemRecordHead>(weapons, record); }
void ItemStock::AddQuickItem(itemdb::QuickItemRecord* record) { AddUnlessPresent<itemdb::ItemRecordHead>(quickItems, record); }
void ItemStock::AddSpell(itemdb::SpellRecord* record) { AddUnlessPresent(spells, record); }
void ItemStock::AddModifier(itemdb::SpellModifierRecord* record) { AddUnlessPresent(modifiers, record); }

void ItemStock::AddMaterial(itemdb::MaterialRecordHead* record, int minCount, int maxCount) {
    if (!record) return;
    StockMaterial* entry = materials.data();
    StockMaterial* end = materials.data() + materials.size();
    while (entry != end && entry->record != record) ++entry;
    if (entry == end) {
        materials.push_back(StockMaterial{record, minCount, maxCount, maxCount});
        return;
    }
    entry->minCount = minCount;
    entry->maxCount = maxCount;
    entry->left = maxCount;
}

// ---- The scratch vectors ---------------------------------------------------------------------------

// Empty at the start; the exit functions that the static initialisers register free their storage
// (docs/notes/standalone.md, "The static initialisers' objects").
namespace stock {
constinit std::vector<itemdb::ItemRecordHead*> g_takenPrototypes = /*007C8680*/ {};
constinit std::vector<StockMaterial*> g_candidates = /*007C8650*/ {};
constinit std::vector<StockMaterial> g_takenMaterials = /*007C8640*/ {};
constinit std::vector<itemdb::SpellRecord*> g_takenSpells = /*007C8670*/ {};
constinit std::vector<itemdb::SpellModifierRecord*> g_takenModifiers = /*007C8660*/ {};
} // namespace stock

void FreeTakenPrototypes() { lib::FreeStorage(stock::g_takenPrototypes); }
void FreeTakenSpells() { lib::FreeStorage(stock::g_takenSpells); }
void FreeTakenModifiers() { lib::FreeStorage(stock::g_takenModifiers); }
void FreeCandidates() { lib::FreeStorage(stock::g_candidates); }
void FreeTakenMaterials() { lib::FreeStorage(stock::g_takenMaterials); }
