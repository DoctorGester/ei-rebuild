// The trader stock generation (0069AE20, 0069A110, 00699810 / 006995E0 / 00699A40): see item_stock.h and docs/notes/items_spells.md
// ("Trader stock"). The STLport containers are the original's: their allocating members are called
// through the game, the pointer copies they inline are written out here.
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
#include "../lib/stlp_pair.h"
#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/stlp_hash.h"
#include "lib/stlp_pair.h"
#include "lib/static_object.h"
#include "lib/nan_audit.h"

#include <cstring>

namespace {

// ---- Original code --------------------------------------------------------------------------------

// The item constructor (CItemConstructor, item_constructor.cpp); MagicCapacity (006A6480): whether
// the prototype and material allow a spell (not for kinds 0x11 / 0x12 without a material), with the
// level (prototype +0x44 + material +0x1C, as a float) and the power limit (prototype +0x40 +
// material +0x18).

// The vectors' element policies: plain pointers and material entries.
using CandidateElement = stlp::PlainElement<StockMaterial*>;
using MaterialElement = stlp::PlainElement<StockMaterial>;

// The output list of counted spells (list<CSpell*>, insert 0069C410): an element is referenced
// while it is held.
struct HeldSpell {
    static void Copy(CSpell** to, CSpell* const& from) {
        *to = from;
        if (from) ++from->refCount_;
    }
    static void Destroy(CSpell** p) {
        if (*p) (*p)->Release();
    }
};

// ---- Globals (item_stock.h, namespace stock) -------------------------------------------------------

using stock::CandidateVector;
using stock::Candidates;
using stock::TakenMaterials;
using stock::TakenPrototypes;


// The template's party object (ItemStock::context): slot 16 the item constructor, 18 the item
// manager, 22 the spell manager.
CParty* PartyOf(const ItemStock* stock) { return stock->context; }
CItemConstructor* ConstructorOf(const ItemStock* stock) { return PartyOf(stock)->ItemConstructor(); }

int Compare(const char* a, const char* b) {
    return gamecrt::mbscmp(a, b);
}
int CompareNoCase(const char* a, const char* b) {
    return gamecrt::mbsicmp(a, b);
}

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

// vector::operator= as the original inlines it (allocate and copy 0069D9A0 / 0069DB00, the spell
// and modifier vectors' 0069DC70 / 0069DDD0; deallocate 0069C990 / 0069C9D0 / 004AA810 /
// 004AA7D0). The callers skip a self-assignment.
template <typename T>
void AssignPointers(stlp::vector<T*>& to, const stlp::vector<T*>& from) { to.template AssignVector<stlp::PlainElement<T*>>(from); }
void AssignMaterials(StockMaterialVector& to, const StockMaterialVector& from) { to.AssignVector<MaterialElement>(from); }

// Appends an item to the output list (list<CItem*>::insert, 0069C3D0; counted by the caller).
void Output(StockList* out, CItem* item) { StockList::Insert(out->End(), item); }

// The item's reference count.
void AddReference(CItem* item) { ++item->refCount_; }

// A prototype record: text08 its name, materialClass its material class, price, materialUnits the material
// units it takes; record slot 0 the item kind.
const char* PrototypeName(const itemdb::ItemRecordHead* p) { return p->text08; }
const char* MaterialClass(const itemdb::ItemRecordHead* p) { return p->materialClass; }
int MaterialUnits(const itemdb::ItemRecordHead* p) { return static_cast<int>(p->materialUnits); }

// Picks the material of a prototype from the candidates (pointers into the stock's entries). Each
// candidate in turn is taken with a chance by its quality q (material +0x10) against the
// prototype's price p: 10% outside [2p/7 - p/5, p/5], else 110 - |(q - p/7) / (p/7 - p/5)| x 100
// percent (truncated). Null when none was taken.
StockMaterial* DrawMaterial(const itemdb::ItemRecordHead* prototype) {
    float seventh = static_cast<float>(prototype->price * stock::SeventhFactor());
    float fifth = static_cast<float>(prototype->price * stock::FifthFactor());
    CandidateVector& c = Candidates();
    if (c.begin() == c.end()) return nullptr;
    float low = static_cast<float>((seventh + seventh) - fifth);
    for (StockMaterial** q = c.begin(); q != c.end(); ++q) {
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

// Empties the spell list as the original's clear does: each counted spell released (slot 4; the
// holder's release 004C17E0 / deleting destructor 0069E540 with 0), each node back to the node pool
// (0069CA50 with 1).
void ClearSpellList(StockSpellList& list) {
    StockSpellListNode* head = list.head;
    StockSpellListNode* node = head->next;
    while (node != head) {
        StockSpellListNode* next = node->next;
        if (node->data) node->data->Release();   // slot 4
        stlp::PushSmallNode(node);
        node = next;
    }
    head->next = head;
    head->prev = head;
}

// Frees the spell list as the original's clear and destructor do: the clear, then the sentinel.
void DestroySpellList(StockSpellList& list) {
    ClearSpellList(list);
    // (The destructor's own clear finds the list empty.)
    stlp::deallocate(list.head, sizeof(StockSpellListNode));
}

// The random spell of a weapon or armor: half the time, when the material allows magic, one of the
// spells 0069A780 generates for the item's level, if within the power limit.
void AddSpell(ItemStock* stock, CItemConstructor* ctor, StockSpellList& spells) {
    if (IsaacRandom::Global().Next() % 101 >= 50) return;
    float level = 0.0f;
    float power = 0.0f;
    if (!ctor->MagicCapacity(&level, &power)) return;
    float levels[8];
    for (float& l : levels) l = level;
    stock->GenerateSpells(&spells, levels, 0);
    unsigned count = 0;
    for (StockSpellListNode* n = spells.head->next; n != spells.head; n = n->next) ++count;
    if (count == 0) return;
    unsigned index = count > 1 ? IsaacRandom::Global().Next() % count : 0;
    StockSpellListNode* node = spells.head->next;
    for (unsigned i = 0; i < index; ++i) node = node->next;
    CSpell* spell = node->data;
    float spellPower = spell->manaCost_;
    EI_NAN_AUDIT(spellPower, power);
    if (spellPower <= power) ctor->SetSpellObject(spell);
}

} // namespace

// ---- 0069AE20 -------------------------------------------------------------------------------------

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
    taken.Finish() = taken.begin();   // erase(begin, end)
    if (prototypes->size() > static_cast<std::size_t>(count)) {
        Shuffle(prototypes->begin(), prototypes->end());
        taken.RangeInsert<stlp::PlainElement<itemdb::ItemRecordHead*>>(taken.begin(), prototypes->begin(), prototypes->begin() + count);
    } else if (prototypes != &taken) {
        AssignPointers(taken, *prototypes);
    }

    CandidateVector& candidates = Candidates();
    for (itemdb::ItemRecordHead** p = taken.begin(); p != taken.end(); ++p) {
        itemdb::ItemRecordHead* prototype = *p;
        if (Compare(PrototypeName(prototype), game::WandText()) == 0) continue;
        ctor->SetManager(manager);
        ctor->BeginCreateItem(itemdb::RecordSlots(prototype)->Kind(), -1);
        ctor->SetPrototype(prototype);
        candidates.Finish() = candidates.begin();   // erase(begin, end)
        if (CompareNoCase(MaterialClass(prototype), game::NoneText()) != 0) {
            for (StockMaterial* e = materials.begin(); e != materials.end(); ++e) {
                if (Compare(MaterialClass(prototype), e->record->soundName) != 0) continue;
                if (MaterialUnits(prototype) > e->left) continue;
                candidates.PushBack<CandidateElement>(e);   // _M_insert_overflow 0069D470
            }
        }
        StockMaterial* chosen = DrawMaterial(prototype);
        if (!chosen) {
            if (CompareNoCase(MaterialClass(prototype), game::NoneText()) != 0) {
                if (candidates.end() == candidates.begin()) {
                    ctor->Cancel();
                    ctor->SetManager(nullptr);
                    continue;
                }
                Shuffle(candidates.begin(), candidates.end());
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
        spellList.Construct();   // 0069C440
        if (kind == 1 || kind == 3 || kind == 0) ::AddSpell(this, ctor, spellList);
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        AddReference(item);
        if (item->type_ == 0x3006 && item->GetCategory() == 8)
            item->count_ = static_cast<std::uint32_t>(gamecrt::rand() * 30 / 0x7FFF % 30 + 20);
        Output(out, item);
        DestroySpellList(spellList);
    }
    candidates.Finish() = candidates.begin();
}
EI_REWRITTEN(0x0069AE20, ItemStock::GenerateItems);

// ---- 00699410 / 0069B570 / 0069B550 / 0069B590 ----------------------------------------------------

void ItemStock::SetLevels(const float* base, const float* scroll) {
    for (int i = 0; i < 8; ++i) baseLevels[i] = base[i];
    for (int i = 0; i < 8; ++i) scrollLevels[i] = scroll[i];
}
EI_REWRITTEN(0x00699410, ItemStock::SetLevels);

void ItemStock::GenerateWeapons(int minCount, int maxCount, StockList* out) { GenerateItems(&weapons, minCount, maxCount, out, 0); }
EI_REWRITTEN(0x0069B570, ItemStock::GenerateWeapons);
void ItemStock::GenerateArmors(int minCount, int maxCount, StockList* out) { GenerateItems(&armors, minCount, maxCount, out, 1); }
EI_REWRITTEN(0x0069B550, ItemStock::GenerateArmors);
void ItemStock::GenerateQuickItems(int minCount, int maxCount, StockList* out) { GenerateItems(&quickItems, minCount, maxCount, out, 2); }
EI_REWRITTEN(0x0069B590, ItemStock::GenerateQuickItems);

// ---- 0069A110 -------------------------------------------------------------------------------------

void ItemStock::GenerateMaterials(int minCount, int maxCount, StockList* out) {
    CItemConstructor* ctor = ConstructorOf(this);
    CItemManager* manager = PartyOf(this)->ItemManager();
    int count = RandomCount(minCount, maxCount);
    StockMaterialVector& taken = TakenMaterials();
    taken.Finish() = taken.begin();   // erase(begin, end)
    if (materials.size() > static_cast<std::size_t>(count)) {
        Shuffle(materials.begin(), materials.end());
        taken.RangeInsert<MaterialElement>(taken.begin(), materials.begin(), materials.begin() + count);   // 0069ECD0
    } else if (&materials != &taken) {
        AssignMaterials(taken, materials);
    }
    itemdb::LootItemRecord* prototype = itemdb::TheItemDatabase().loot.Named(game::MaterialText());
    for (StockMaterial* e = taken.begin(); e != taken.end(); ++e) {
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
    taken.Finish() = taken.begin();
}
EI_REWRITTEN(0x0069A110, ItemStock::GenerateMaterials);

// ---- 00699810 / 006995E0 / 00699A40 ---------------------------------------------------------------

namespace {

// The common body of the three instruction generators: the source records are copied to the
// scratch 007C8680, then each (except one named `skip`) becomes an item of kind 0x3007 with the
// class-0x15 prototype named at `prototypeName`, no material, +0x54 the record's index in the table
// `table` and +0x48 a random count.
template <typename Record>
void GenerateInstructions(ItemStock* stock, const StockPrototypeVector& source, const char* prototypeName,
                          const itemdb::NamedRecordTable<Record>& table, const char* skip, int minCount, int maxCount,
                          StockList* out) {
    CItemConstructor* ctor = ConstructorOf(stock);
    CItemManager* manager = PartyOf(stock)->ItemManager();
    StockPrototypeVector& taken = TakenPrototypes();
    taken.Finish() = taken.begin();   // erase(begin, end)
    if (&source != &taken) AssignPointers(taken, source);
    itemdb::LootItemRecord* prototype = itemdb::TheItemDatabase().loot.Named(prototypeName);
    for (itemdb::ItemRecordHead** p = taken.begin(); p != taken.end(); ++p) {
        if (skip && Compare(PrototypeName(*p), skip) == 0) continue;
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
    taken.Finish() = taken.begin();
}

} // namespace

void ItemStock::GenerateWeaponInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, weapons, game::InstructionWeaponText(), itemdb::TheItemDatabase().weapons, nullptr, minCount,
                         maxCount, out);
}
EI_REWRITTEN(0x00699810, ItemStock::GenerateWeaponInstructions);

void ItemStock::GenerateArmorInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, armors, game::InstructionArmorText(), itemdb::TheItemDatabase().armors, nullptr, minCount,
                         maxCount, out);
}
EI_REWRITTEN(0x006995E0, ItemStock::GenerateArmorInstructions);

// Quick items named "scroll" get no instruction.
void ItemStock::GenerateQuickItemInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, quickItems, game::InstructionQuickItemText(), itemdb::TheItemDatabase().quickItems,
                         game::ScrollText(), minCount, maxCount, out);
}
EI_REWRITTEN(0x00699A40, ItemStock::GenerateQuickItemInstructions);

// ---- 0069A480 / 0069A780 --------------------------------------------------------------------------

namespace {

// The spell recipe map (+0x50): clear 004AAEB0, find-or-insert of a {name, list} pair 0069D130,
// begin 0069C160 and the next non-empty bucket 0069D0E0. Its lists: constructor 0069C680,
// destructor 0069C830, end 0069C2E0, insert 0069C2F0; the pair: constructor 0069C1C0 (the record,
// whose name starts it, and &list), destructor 004AD640 (stlp_pair.cpp). The modifier map
// (+0x64): clear 004AB2B0, operator[] 0069D1E0 with a {key, null} pair.
// The spell prototypes by name (thiscall on 007C8510, 00613920) and the spell manager's builder
// (00689310, RET 8: prototype, modifier list; a new counted spell or null).
itemdb::SpellRecord* FindSpellPrototype(const char* name) {
    return itemdb::TheSpellDatabase().spells.Named(name);
}
// The modifier list (list<modifier*>): constructor 0069BF70, end 0068C500, insert 0068C560, node
// free 0068C640. The output list of counted spells: end 0069BE60, insert 0069C410.

// The modifier map's entry for `name` (created null when missing, as operator[] does).
itemdb::SpellModifierRecord*& ModifierSlot(ItemStock* stock, const stlp::string& name) {
    return stlp::NamedSlot<itemdb::SpellModifierRecord>(stock->modifierMap, name);
}

// A modifier record (itemdb::SpellModifierRecord): its code's first letter the kind, mana the cost
// added, strength (a cost factor for kind 'm'), skill the level it adds.
using Modifier = itemdb::SpellModifierRecord;
char ModifierKind(const Modifier* m) { return m->code[0]; }
float ModifierCost(const Modifier* m) { return m->mana; }
float ModifierFactor(const Modifier* m) { return m->strength; }
int ModifierLevel(const Modifier* m) { return m->skill; }

void AppendModifier(SpellModifierList& list, Modifier* modifier) { SpellModifierList::Insert(list.End(), modifier); }

void ClearModifierList(SpellModifierList& list) { list.ClearWith([](Modifier**) {}); }

} // namespace

// Original bug kept: the 004ABD00 lookup inserts a null entry into 007C851C for every recipe name
// it does not know.
void ItemStock::FillSpellMaps(const SpellRecipeVector* recipes) {
    spellMap.Clear();
    modifierMap.Clear();
    for (SpellRecipe* recipe = recipes->begin(); recipe != recipes->end(); ++recipe) {
        if (!(recipe->classMask & (1u << (classIndex & 31)))) continue;
        itemdb::SpellRecord* spell = stlp::NamedSlot<itemdb::SpellRecord>(stock::SpellsByName(), recipe->spell);   // keyed by the recipe's spell name
        if (!spell) continue;
        bool offered = false;
        for (itemdb::SpellRecord** s = spells.begin(); s != spells.end(); ++s) {
            if (*s == spell) {
                offered = true;
                break;
            }
        }
        if (!offered) continue;
        StockRecipeList list;
        list.Construct();
        lib::InPlace<StockRecipeMap::Value> pair;
        pair->Construct(recipe->spell, list);
        StockRecipeList* entries = &spellMap.FindOrInsert(pair.object).mapped;
        pair->Destruct();
        list.Release();
        StockRecipeList::Insert(entries->End(), recipe);   // the recipe lists hold raw record pointers
    }

    for (Modifier** p = modifiers.begin(); p != modifiers.end(); ++p) {
        Modifier* m = *p;
        char letters[2] = {m->code[0], m->code[1]};
        switch (ModifierKind(m)) {
        case 'a': case 'd': case 'e': case 'm': case 'r': case 't': {
            stlp::string key(letters, letters + 1);
            Modifier* held = ModifierSlot(this, key);
            // Replaced when the held one's factor (+0x14) is less.
            if (held) EI_NAN_AUDIT(ModifierFactor(held), ModifierFactor(m));
            if (!held || ModifierFactor(held) < ModifierFactor(m)) ModifierSlot(this, key) = m;
            break;
        }
        case 'f': case 'i': {
            stlp::string key(letters, letters + 2);
            ModifierSlot(this, key) = m;
            break;
        }
        default:
            break;
        }
    }
}
EI_REWRITTEN(0x0069A480, ItemStock::FillSpellMaps);

void ItemStock::GenerateSpells(StockSpellList* out, const float* levels, unsigned threshold) {
    CSpellManager* spellManager = PartyOf(this)->SpellManager();
    SpellModifierList chosen;
    chosen.Construct();
    for (StockRecipeMap::Node* recipes = spellMap.First(); recipes; recipes = spellMap.Next(recipes)) {
        StockRecipeList::Node* head = recipes->value.mapped.head;
        for (StockRecipeList::Node* n = head->next; n != head; n = n->next) {
            if (IsaacRandom::Global().Next() % 101 < threshold) continue;
            ClearModifierList(chosen);
            int count = 0;
            auto* recipe = static_cast<const SpellRecipe*>(n->data);
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
                int reached = level;   // (the original keeps it in a register; `level` is not updated)
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
            CSpell* spell = spellManager->CreateSpell(prototype, &chosen);
            if (spell) ++spell->refCount_;
            StockSpellList::Insert<HeldSpell>(out->End(), spell);
            if (spell) spell->Release();   // slot 4
        }
    }
    ClearModifierList(chosen);
    if (chosen.head) stlp::deallocate(chosen.head, sizeof(SpellModifierNode));
}
EI_REWRITTEN(0x0069A780, ItemStock::GenerateSpells);

// ---- 0069B5B0 -------------------------------------------------------------------------------------

namespace {

// CStream::ReadString (00461190): a CString constructed at `out` from the bytes up to a zero; the
// stream's Read is virtual slot 1.
// The record lookups by name (thiscall on the tables; null when missing): armors 004AB820 on
// 007C8718, weapons 004AB720 on 007C86F8, quick items 00613820 on 007C8738, materials 0069D290 on
// 007C86D8; the spell and modifier maps' operator[] (004ABD00 on 007C851C, 00684B20 on 007C853C;
// they insert a null for a new name).
// (NamedRecordTable::Named.)
// push_back of the pointer vectors: 0069BD30 (the three item lists), 0069BDC0 (spells), 0069BDF0
// (modifiers); vector<StockMaterial>::_M_insert_overflow 0069CC20.

// The counts and the material values are read into locals the original reuses for every section,
// so a failed read leaves the previous value.
void ReadInt(CStream* stream, void* value) { stream->Read(value, 4u); }

// The next string of the stream into `name` (through a temporary, as the original).
void ReadName(CStream* stream, mfc::CString& name) {
    alignas(mfc::CString) unsigned char temporary[sizeof(mfc::CString)];
    mfc::CString* read = stream->ReadString(temporary);
    name = *read;
    read->~CString();
}

// std::find (0069E350..0069E3D0, one instantiation per list), then push_back when missing.
template <typename T>
void AddUnique(stlp::vector<T*>& list, T* record) {
    T** p = list.begin();
    while (p != list.end() && *p != record) ++p;
    if (p == list.end()) list.template PushBack<stlp::PlainElement<T*>>(record);
}

// A record section: a count, then names lower-cased and looked up in `table`.
template <typename Record>
void LoadRecords(CStream* stream, mfc::CString& name, unsigned& count, itemdb::NamedRecordTable<Record>& table,
                 StockPrototypeVector& list) {
    ReadInt(stream, &count);
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        name.MakeLower();
        itemdb::ItemRecordHead* record = table.Named(name);
        if (record) AddUnique(list, record);
    }
}

// A map section: a count, then names (as read) looked up in an STLport map by string.
template <typename Record, typename Map>
void LoadMapped(CStream* stream, mfc::CString& name, unsigned& count, Map& map, stlp::vector<Record*>& list) {
    ReadInt(stream, &count);
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        Record* record;
        {
            stlp::string key(name.GetString());
            record = stlp::NamedSlot<Record>(map, key);
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
    mfc::CString name;
    unsigned count = 0;
    itemdb::ItemDatabase& items = itemdb::TheItemDatabase();
    LoadRecords(stream, name, count, items.armors, armors);
    LoadRecords(stream, name, count, items.weapons, weapons);
    LoadRecords(stream, name, count, items.quickItems, quickItems);

    ReadInt(stream, &count);
    int values[3] = {0, 0, 0};
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        name.MakeLower();
        StockMaterial entry;
        for (int& v : values) ReadInt(stream, &v);
        entry.record = items.materials.Named(name);
        entry.minCount = values[0];
        entry.maxCount = values[1];
        entry.left = values[2];
        materials.PushBack<MaterialElement>(entry);
    }

    LoadMapped(stream, name, count, stock::SpellsByName(), spells);
    LoadMapped(stream, name, count, stock::ModifiersByName(), modifiers);
}
EI_REWRITTEN(0x0069B5B0, ItemStock::LoadLists);

// ---- 0069BAC0 (A-H17) ------------------------------------------------------------------------------

namespace {

void WriteInt(CStream* stream, std::uint32_t value) { stream->Write(&value, 4u); }
// A text with its terminating zero.
void WriteText(CStream* stream, const char* text) {
    stream->Write(text, static_cast<std::uint32_t>(std::strlen(text) + 1));
}

void SaveItemRecords(CStream* stream, const StockPrototypeVector& list) {
    WriteInt(stream, static_cast<std::uint32_t>(list.size()));
    for (itemdb::ItemRecordHead** p = list.begin(); p != list.end(); ++p) WriteText(stream, (*p)->text04);
}

} // namespace

void ItemStock::SaveLists(CStream* out) {
    SaveItemRecords(out, armors);
    SaveItemRecords(out, weapons);
    SaveItemRecords(out, quickItems);

    WriteInt(out, static_cast<std::uint32_t>(materials.size()));
    for (StockMaterial* e = materials.begin(); e != materials.end(); ++e) {
        WriteText(out, e->record->key);
        WriteInt(out, static_cast<std::uint32_t>(e->minCount));
        WriteInt(out, static_cast<std::uint32_t>(e->maxCount));
        WriteInt(out, static_cast<std::uint32_t>(e->left));
    }

    WriteInt(out, static_cast<std::uint32_t>(spells.size()));
    for (itemdb::SpellRecord** p = spells.begin(); p != spells.end(); ++p) WriteText(out, (*p)->code);

    // The modifiers' codes go through a CString made from a stack copy of their first four bytes
    // (the original's fifth byte is an uninitialised stack byte, its sixth a zero; ours stops
    // after the four).
    WriteInt(out, static_cast<std::uint32_t>(modifiers.size()));
    for (Modifier** p = modifiers.begin(); p != modifiers.end(); ++p) {
        char code[5] = {};
        std::memcpy(code, (*p)->code, 4);
        mfc::CString text(code);
        WriteText(out, text);
    }
}
EI_REWRITTEN(0x0069BAC0, ItemStock::SaveLists);

// ---- 006993B0 / 00699440 --------------------------------------------------------------------------

void ItemStock::ResetMaterialUnits() {
    for (StockMaterial* e = materials.begin(); e != materials.end(); ++e) {
        e->left = e->maxCount;
        if (e->maxCount > e->minCount) e->left = RandomCount(e->minCount, e->maxCount);
    }
}
EI_REWRITTEN(0x006993B0, ItemStock::ResetMaterialUnits);

void ItemStock::CopyMagic(const ItemStock* other) {
    if (&other->spells != &spells) AssignPointers(spells, other->spells);
    if (&other->modifiers != &modifiers) AssignPointers(modifiers, other->modifiers);
}
EI_REWRITTEN(0x00699440, ItemStock::CopyMagic);

// ---- 00699C90 / 00699ED0 --------------------------------------------------------------------------

namespace {

// The offered spells' and modifiers' records (from the maps by name) have the tables' layouts:
// the spell's code a CString at +4, the modifier's inline at +4.
int SpellIndex(const itemdb::SpellRecord* record) {
    const char* code = record->code;
    itemdb::SpellTable& spells = itemdb::TheSpellDatabase().spells;
    return spells.IndexOf(spells.FindByCode(code));
}
int ModifierIndex(const Modifier* record) {
    const char* code = record->code;
    itemdb::SpellModifierTable& modifiers = itemdb::TheSpellDatabase().modifiers;
    return modifiers.IndexOf(modifiers.FindByCode(code));
}

// The common body of 00699C90 / 00699ED0: the source copied to its scratch vector (007C8670 spells,
// 007C8660 modifiers), then an item of kind 0x3007 with the class-0x15 prototype named at
// `prototypeName` per record, +0x54 its index, +0x48 a random count. The scratch is emptied at the end.
template <typename Record>
void GenerateMagicInstructions(ItemStock* stock, const stlp::vector<Record*>& source, stlp::vector<Record*>& taken,
                               const char* prototypeName, int (*indexOf)(const Record*), int minCount, int maxCount,
                               StockList* out) {
    CItemConstructor* ctor = ConstructorOf(stock);
    CItemManager* manager = PartyOf(stock)->ItemManager();
    PartyOf(stock)->SpellManager();   // (fetched and not used)
    taken.Finish() = taken.begin();   // erase(begin, end)
    if (&source != &taken) AssignPointers(taken, source);
    itemdb::LootItemRecord* prototype = itemdb::TheItemDatabase().loot.Named(prototypeName);
    for (Record** p = taken.begin(); p != taken.end(); ++p) {
        ctor->SetManager(manager);
        ctor->BeginCreateItem(0x3007, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(nullptr);
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        AsLoot(item)->index_ = static_cast<std::uint32_t>(indexOf(*p));
        item->count_ = static_cast<std::uint32_t>(RandomCount(minCount, maxCount));
        AddReference(item);
        Output(out, item);
    }
    taken.Finish() = taken.begin();
}

} // namespace

void ItemStock::GenerateSpellInstructions(int minCount, int maxCount, StockList* out) {
    GenerateMagicInstructions(this, spells, stock::TakenSpells(), game::PrototypeText(), SpellIndex, minCount, maxCount, out);
}
EI_REWRITTEN(0x00699C90, ItemStock::GenerateSpellInstructions);

void ItemStock::GenerateModifierInstructions(int minCount, int maxCount, StockList* out) {
    GenerateMagicInstructions(this, modifiers, stock::TakenModifiers(), game::RuneText(), ModifierIndex, minCount, maxCount, out);
}
EI_REWRITTEN(0x00699ED0, ItemStock::GenerateModifierInstructions);

// ---- 0069AC20 -------------------------------------------------------------------------------------

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
    list.Construct();
    for (int tries = 0;;) {
        ClearSpellList(list);
        GenerateSpells(&list, scrollLevels, 0x32);
        unsigned count = 0;
        for (StockSpellListNode* n = list.head->next; n != list.head; n = n->next) ++count;
        if (count >= static_cast<unsigned>(spells.size())) break;
        if (++tries >= kScrollTries) break;
    }
    itemdb::QuickItemRecord* prototype = itemdb::TheItemDatabase().quickItems.Named(game::SpellContainerText());
    for (StockSpellListNode* n = list.head->next; n != list.head; n = n->next) {
        ctor->SetManager(manager);
        ctor->BeginCreateItem(0x3008, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(nullptr);
        ctor->SetSpellObject(n->data);
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        AddReference(item);
        Output(out, item);
    }
    DestroySpellList(list);
}
EI_REWRITTEN(0x0069AC20, ItemStock::GenerateScrolls);

// --- A-H25: the stock's lists filled by the class template (004A7080) ------------------------------

namespace {

template <typename T>
void AddUnlessPresent(stlp::vector<T*>& list, T* record) {
    if (!record) return;
    T** p = list.begin();
    while (p != list.end() && *p != record) ++p;
    if (p == list.end()) list.push_back(record);  // grows as _M_insert_overflow does
}

} // namespace

void ItemStock::AddArmor(itemdb::ArmorRecord* record) { AddUnlessPresent<itemdb::ItemRecordHead>(armors, record); }
void ItemStock::AddWeapon(itemdb::WeaponRecord* record) { AddUnlessPresent<itemdb::ItemRecordHead>(weapons, record); }
void ItemStock::AddQuickItem(itemdb::QuickItemRecord* record) { AddUnlessPresent<itemdb::ItemRecordHead>(quickItems, record); }
void ItemStock::AddSpell(itemdb::SpellRecord* record) { AddUnlessPresent(spells, record); }
void ItemStock::AddModifier(itemdb::SpellModifierRecord* record) { AddUnlessPresent(modifiers, record); }
EI_REWRITTEN(0x00699080, ItemStock::AddArmor);
EI_REWRITTEN(0x006990E0, ItemStock::AddWeapon);
EI_REWRITTEN(0x00699140, ItemStock::AddQuickItem);
EI_REWRITTEN(0x006991A0, ItemStock::AddSpell);
EI_REWRITTEN(0x00699200, ItemStock::AddModifier);

void ItemStock::AddMaterial(itemdb::MaterialRecordHead* record, int minCount, int maxCount) {
    if (!record) return;
    StockMaterial* entry = materials.begin();
    while (entry != materials.end() && entry->record != record) ++entry;
    if (entry == materials.end()) {
        materials.push_back(StockMaterial{record, minCount, maxCount, maxCount});
        return;
    }
    entry->minCount = minCount;
    entry->maxCount = maxCount;
    entry->left = maxCount;
}
EI_REWRITTEN(0x00699260, ItemStock::AddMaterial);

// ---- The scratch vectors (item_stock.h) ---------------------------------------------------------------------------

// Empty at the start, as the static initialisers 00698E50-00698F90 leave them; the exit functions they register free
// their storage (docs/notes/standalone.md, "The static initialisers' objects").
namespace stock {
constinit stlp::vector<itemdb::ItemRecordHead*> g_takenPrototypes = /*007C8680*/ {};
constinit stlp::vector<StockMaterial*> g_candidates = /*007C8650*/ {};
constinit stlp::vector<StockMaterial> g_takenMaterials = /*007C8640*/ {};
constinit stlp::vector<itemdb::SpellRecord*> g_takenSpells = /*007C8670*/ {};
constinit stlp::vector<itemdb::SpellModifierRecord*> g_takenModifiers = /*007C8660*/ {};
} // namespace stock

namespace {
void __cdecl FreeTakenPrototypes() { stock::g_takenPrototypes.FreeStorage(); }   // 00698E90
void __cdecl FreeTakenSpells() { stock::g_takenSpells.FreeStorage(); }           // 00698EE0
void __cdecl FreeTakenModifiers() { stock::g_takenModifiers.FreeStorage(); }     // 00698F30
void __cdecl FreeCandidates() { stock::g_candidates.FreeStorage(); }             // 00698F80
void __cdecl FreeTakenMaterials() { stock::g_takenMaterials.FreeStorage(); }     // 00698FD0
} // namespace

EI_REPLACE(0x00698E90, FreeTakenPrototypes);
EI_REPLACE(0x00698EE0, FreeTakenSpells);
EI_REPLACE(0x00698F30, FreeTakenModifiers);
EI_REPLACE(0x00698F80, FreeCandidates);
EI_REPLACE(0x00698FD0, FreeTakenMaterials);
