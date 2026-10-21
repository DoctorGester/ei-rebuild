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
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "lib/nan_audit.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// ---- Original code --------------------------------------------------------------------------------

// The item constructor (CItemConstructor, item_constructor.cpp); MagicCapacity (006A6480): whether
// the prototype and material allow a spell (not for kinds 0x11 / 0x12 without a material), with the
// level (prototype +0x44 + material +0x1C, as a float) and the power limit (prototype +0x40 +
// material +0x18).

// The vectors' element policies: plain pointers and material entries.
using PtrElement = stlp::PlainElement<char*>;
using CandidateElement = stlp::PlainElement<StockMaterial*>;
using MaterialElement = stlp::PlainElement<StockMaterial>;

// The output list of counted spells (list<CSpell*>, insert 0069C410): an element is referenced
// while it is held.
struct HeldSpell {
    static void Copy(void** to, void* const& from) {
        *to = from;
        if (from) ++static_cast<CEngineObject*>(from)->refCount_;
    }
    static void Destroy(void** p) {
        if (*p) static_cast<CEngineObject*>(*p)->Release();
    }
};

// ---- Globals (item_stock.h, namespace stock) -------------------------------------------------------

using stock::CandidateVector;
using stock::Candidates;
using stock::TakenMaterials;
using stock::TakenPrototypes;


// The record tables the original passes as `this` to their lookups.
void* TableObject(const RecordTable& table) { return const_cast<RecordTable*>(&table); }

// The template's party object (ItemStock::context): slot 16 the item constructor, 18 the item
// manager, 22 the spell manager.
CParty* PartyOf(const ItemStock* stock) { return static_cast<CParty*>(static_cast<CEngineObject*>(stock->context)); }
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
void AssignPointers(StockPtrVector& to, const StockPtrVector& from) { to.AssignVector<PtrElement>(from); }
void AssignMaterials(StockMaterialVector& to, const StockMaterialVector& from) { to.AssignVector<MaterialElement>(from); }

// Appends an item to the output list (list<CItem*>::insert, 0069C3D0; counted by the caller).
void Output(StockList* out, CItem* item) { StockList::Insert(out->End(), static_cast<void*>(item)); }

// The item's reference count.
void AddReference(CItem* item) { ++item->refCount_; }

// A prototype record: text08 its name, materialClass its material class, price, materialUnits the material
// units it takes; record slot 0 the item kind.
itemdb::ItemRecordHead* Record(char* p) { return reinterpret_cast<itemdb::ItemRecordHead*>(p); }
const char* PrototypeName(char* p) { return Record(p)->text08; }
const char* MaterialClass(char* p) { return Record(p)->materialClass; }
int MaterialUnits(char* p) { return static_cast<int>(Record(p)->materialUnits); }
const itemdb::MaterialRecordHead* Material(const StockMaterial* e) {
    return reinterpret_cast<const itemdb::MaterialRecordHead*>(e->record);
}

// Picks the material of a prototype from the candidates (pointers into the stock's entries). Each
// candidate in turn is taken with a chance by its quality q (material +0x10) against the
// prototype's price p: 10% outside [2p/7 - p/5, p/5], else 110 - |(q - p/7) / (p/7 - p/5)| x 100
// percent (truncated). Null when none was taken.
StockMaterial* DrawMaterial(char* prototype) {
    float seventh = static_cast<float>(Record(prototype)->price * stock::SeventhFactor());
    float fifth = static_cast<float>(Record(prototype)->price * stock::FifthFactor());
    CandidateVector& c = Candidates();
    if (c.begin() == c.end()) return nullptr;
    float low = static_cast<float>((seventh + seventh) - fifth);
    for (StockMaterial** q = c.begin(); q != c.end(); ++q) {
        int chance = 10;
        float quality = Material(*q)->unitPrice;
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
void ClearSpellList(StockList& list) {
    StockListNode* head = list.head;
    StockListNode* node = head->next;
    while (node != head) {
        StockListNode* next = node->next;
        if (node->data) static_cast<CEngineObject*>(node->data)->Release();   // slot 4
        node->next = static_cast<StockListNode*>(stlp::SmallNodeFreeList());
        stlp::SmallNodeFreeList() = node;
        node = next;
    }
    head->next = head;
    head->prev = head;
}

// Frees the spell list as the original's clear and destructor do: the clear, then the sentinel.
void DestroySpellList(StockList& list) {
    ClearSpellList(list);
    // (The destructor's own clear finds the list empty.)
    stlp::deallocate(list.head, sizeof(StockListNode));
}

// The random spell of a weapon or armor: half the time, when the material allows magic, one of the
// spells 0069A780 generates for the item's level, if within the power limit.
void AddSpell(ItemStock* stock, CItemConstructor* ctor, StockList& spells) {
    if (IsaacRandom::Global().Next() % 101 >= 50) return;
    float level = 0.0f;
    float power = 0.0f;
    if (!ctor->MagicCapacity(&level, &power)) return;
    float levels[8];
    for (float& l : levels) l = level;
    stock->GenerateSpells(&spells, levels, 0);
    unsigned count = 0;
    for (StockListNode* n = spells.head->next; n != spells.head; n = n->next) ++count;
    if (count == 0) return;
    unsigned index = count > 1 ? IsaacRandom::Global().Next() % count : 0;
    StockListNode* node = spells.head->next;
    for (unsigned i = 0; i < index; ++i) node = node->next;
    void* spell = node->data;
    float spellPower;   // CSpell +0x40, the prototype's +0x18 as float bits
    std::memcpy(&spellPower, &static_cast<CSpell*>(static_cast<CEngineObject*>(spell))->manaCost_, 4);
    EI_NAN_AUDIT(spellPower, power);
    if (spellPower <= power) ctor->SetSpellObject(static_cast<CSpell*>(spell));
}

} // namespace

// ---- 0069AE20 -------------------------------------------------------------------------------------

// Original bug kept: a failed build (EndCreateItem null) is dereferenced.
void ItemStock::GenerateItems(StockPtrVector* prototypes, int minCount, int maxCount, StockList* out, int kind) {
    CItemConstructor* ctor = ConstructorOf(this);
    void* manager = PartyOf(this)->ItemManager();
    if (kind == 0 || kind == 3)
        FillSpellMaps(stock::WeaponSpellRecipes());
    else if (kind == 1)
        FillSpellMaps(stock::ArmorSpellRecipes());

    int count = RandomCount(minCount, maxCount);
    StockPtrVector& taken = TakenPrototypes();
    taken.Finish() = taken.begin();   // erase(begin, end)
    if (prototypes->size() > static_cast<std::size_t>(count)) {
        Shuffle(prototypes->begin(), prototypes->end());
        taken.RangeInsert<stlp::PlainElement<char*>>(taken.begin(), prototypes->begin(), prototypes->begin() + count);
    } else if (prototypes != &taken) {
        AssignPointers(taken, *prototypes);
    }

    CandidateVector& candidates = Candidates();
    for (char** p = taken.begin(); p != taken.end(); ++p) {
        char* prototype = *p;
        if (Compare(PrototypeName(prototype), game::WandText()) == 0) continue;
        ctor->SetManager(static_cast<CBaseManager*>(manager));
        ctor->BeginCreateItem(itemdb::RecordSlots(Record(prototype))->Kind(), -1);
        ctor->SetPrototype(prototype);
        candidates.Finish() = candidates.begin();   // erase(begin, end)
        if (CompareNoCase(MaterialClass(prototype), game::NoneText()) != 0) {
            for (StockMaterial* e = materials.begin(); e != materials.end(); ++e) {
                if (Compare(MaterialClass(prototype), Material(e)->soundName) != 0) continue;
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

        StockList spellList;
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
EI_REPLACE_METHOD(0x00699410, ItemStock::SetLevels);

void ItemStock::GenerateWeapons(int minCount, int maxCount, StockList* out) { GenerateItems(&weapons, minCount, maxCount, out, 0); }
EI_REPLACE_METHOD(0x0069B570, ItemStock::GenerateWeapons);
void ItemStock::GenerateArmors(int minCount, int maxCount, StockList* out) { GenerateItems(&armors, minCount, maxCount, out, 1); }
EI_REPLACE_METHOD(0x0069B550, ItemStock::GenerateArmors);
void ItemStock::GenerateQuickItems(int minCount, int maxCount, StockList* out) { GenerateItems(&quickItems, minCount, maxCount, out, 2); }
EI_REPLACE_METHOD(0x0069B590, ItemStock::GenerateQuickItems);

// ---- 0069A110 -------------------------------------------------------------------------------------

void ItemStock::GenerateMaterials(int minCount, int maxCount, StockList* out) {
    CItemConstructor* ctor = ConstructorOf(this);
    void* manager = PartyOf(this)->ItemManager();
    int count = RandomCount(minCount, maxCount);
    StockMaterialVector& taken = TakenMaterials();
    taken.Finish() = taken.begin();   // erase(begin, end)
    if (materials.size() > static_cast<std::size_t>(count)) {
        Shuffle(materials.begin(), materials.end());
        taken.RangeInsert<MaterialElement>(taken.begin(), materials.begin(), materials.begin() + count);   // 0069ECD0
    } else if (&materials != &taken) {
        AssignMaterials(taken, materials);
    }
    char* prototype = static_cast<char*>(stlp::RecordNamed(static_cast<stlp::RecordsByName*>(TableObject(itemdb::Loot())), nullptr, game::MaterialText()));
    for (StockMaterial* e = taken.begin(); e != taken.end(); ++e) {
        ctor->SetManager(static_cast<CBaseManager*>(manager));
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
// `table` (records of `recordSize` bytes) and +0x48 a random count.
void GenerateInstructions(ItemStock* stock, const StockPtrVector& source, const char* prototypeName, const RecordTable& table,
                          int recordSize, const char* skip, int minCount, int maxCount, StockList* out) {
    CItemConstructor* ctor = ConstructorOf(stock);
    void* manager = PartyOf(stock)->ItemManager();
    StockPtrVector& taken = TakenPrototypes();
    taken.Finish() = taken.begin();   // erase(begin, end)
    if (&source != &taken) AssignPointers(taken, source);
    char* prototype = static_cast<char*>(stlp::RecordNamed(static_cast<stlp::RecordsByName*>(TableObject(itemdb::Loot())), nullptr, prototypeName));
    for (char** p = taken.begin(); p != taken.end(); ++p) {
        if (skip && Compare(PrototypeName(*p), skip) == 0) continue;
        ctor->SetManager(static_cast<CBaseManager*>(manager));
        ctor->BeginCreateItem(0x3007, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(nullptr);
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        std::intptr_t offset = reinterpret_cast<std::intptr_t>(*p) - reinterpret_cast<std::intptr_t>(table.begin);
        static_cast<CItemLoot*>(item)->index_ = static_cast<std::uint32_t>(static_cast<int>(offset) / recordSize);
        item->count_ = static_cast<std::uint32_t>(RandomCount(minCount, maxCount));
        AddReference(item);
        Output(out, item);
    }
    taken.Finish() = taken.begin();
}

} // namespace

void ItemStock::GenerateWeaponInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, weapons, game::InstructionWeaponText(), itemdb::Weapons(), itemdb::kWeaponSize, nullptr,
                         minCount, maxCount, out);
}
EI_REWRITTEN(0x00699810, ItemStock::GenerateWeaponInstructions);

void ItemStock::GenerateArmorInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, armors, game::InstructionArmorText(), itemdb::Armors(), itemdb::kArmorSize, nullptr, minCount,
                         maxCount, out);
}
EI_REWRITTEN(0x006995E0, ItemStock::GenerateArmorInstructions);

// Quick items named "scroll" get no instruction.
void ItemStock::GenerateQuickItemInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, quickItems, game::InstructionQuickItemText(), itemdb::QuickItems(), itemdb::kQuickItemSize,
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
    return reinterpret_cast<itemdb::SpellRecord*>(static_cast<char*>(stlp::RecordNamed(static_cast<stlp::RecordsByName*>(TableObject(itemdb::Spells())), nullptr, name)));
}
// The modifier list (list<modifier*>): constructor 0069BF70, end 0068C500, insert 0068C560, node
// free 0068C640. The output list of counted spells: end 0069BE60, insert 0069C410.

// The modifier map's entry for `name` (created null when missing, as operator[] does).
char*& ModifierSlot(ItemStock* stock, const stlp::string& name) { return NamedRecord(stock->modifierMap, name); }

// A modifier record (itemdb::SpellModifierRecord): its code's first letter the kind, mana the cost
// added, strength (a cost factor for kind 'm'), skill the level it adds.
const itemdb::SpellModifierRecord* Modifier(char* m) { return reinterpret_cast<const itemdb::SpellModifierRecord*>(m); }
char ModifierKind(char* m) { return Modifier(m)->code[0]; }
float ModifierCost(char* m) { return Modifier(m)->mana; }
float ModifierFactor(char* m) { return Modifier(m)->strength; }
int ModifierLevel(char* m) { return Modifier(m)->skill; }

void AppendModifier(StockList& list, char* modifier) { StockList::Insert(list.End(), static_cast<void*>(modifier)); }

void ClearModifierList(StockList& list) { list.ClearWith([](void**) {}); }

} // namespace

// Original bug kept: the 004ABD00 lookup inserts a null entry into 007C851C for every recipe name
// it does not know.
void ItemStock::FillSpellMaps(const SpellRecipeVector* recipes) {
    spellMap.Clear();
    modifierMap.Clear();
    for (SpellRecipe* recipe = recipes->begin(); recipe != recipes->end(); ++recipe) {
        if (!(recipe->classMask & (1u << (classIndex & 31)))) continue;
        char* spell = NamedRecord(stock::SpellsByName(), recipe->spell);   // keyed by the recipe's spell name
        if (!spell) continue;
        bool offered = false;
        for (char** s = spells.begin(); s != spells.end(); ++s) {
            if (*s == spell) {
                offered = true;
                break;
            }
        }
        if (!offered) continue;
        char* value = reinterpret_cast<char*>(recipe);   // the recipe lists hold raw record pointers
        StockList list;
        list.Construct();
        alignas(StockRecipeMap::Value) std::uint8_t storage[sizeof(StockRecipeMap::Value)];
        auto* pair = reinterpret_cast<StockRecipeMap::Value*>(storage);
        stlp::PairAbi::ConstructStringList(reinterpret_cast<stlp::StringListPair*>(pair), nullptr, &recipe->spell, &list);
        StockList* entries = &spellMap.FindOrInsert(*pair).mapped;
        stlp::PairAbi::DestroyStringList(reinterpret_cast<stlp::StringListPair*>(pair));
        list.Release();
        StockList::Insert(entries->End(), static_cast<void*>(value));
    }

    for (char** p = modifiers.begin(); p != modifiers.end(); ++p) {
        char* m = *p;
        char letters[2] = {Modifier(m)->code[0], Modifier(m)->code[1]};
        switch (ModifierKind(m)) {
        case 'a': case 'd': case 'e': case 'm': case 'r': case 't': {
            stlp::string key(letters, letters + 1);
            char* held = ModifierSlot(this, key);
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

void ItemStock::GenerateSpells(StockList* out, const float* levels, unsigned threshold) {
    void* spellManager = PartyOf(this)->SpellManager();
    StockList chosen;
    chosen.Construct();
    for (StockRecipeMap::Node* recipes = spellMap.First(); recipes; recipes = spellMap.Next(recipes)) {
        StockListNode* head = recipes->value.mapped.head;
        for (StockListNode* n = head->next; n != head; n = n->next) {
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
                char* m = ModifierSlot(this, required[i]);
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
                char* m = ModifierSlot(this, optional[i]);
                if (!m || budget < ModifierLevel(m) + level) continue;
                AppendModifier(chosen, m);
                cost = static_cast<float>(cost + ModifierCost(m));
                ++count;
                level += ModifierLevel(m);
            }
            char* repeated = ModifierSlot(this, recipe->repeated);
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
            CEngineObject* spell = static_cast<CSpellManager*>(spellManager)
                                       ->CreateSpell(reinterpret_cast<char*>(prototype),
                                                     reinterpret_cast<SpellModifierNode**>(&chosen));
            if (spell) ++spell->refCount_;
            StockList::Insert<HeldSpell>(out->End(), static_cast<void*>(spell));
            if (spell) spell->Release();   // slot 4
        }
    }
    ClearModifierList(chosen);
    if (chosen.head) stlp::deallocate(chosen.head, sizeof(StockListNode));
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
char* FindNamed(const RecordTable& table, const char* name) {
    return (char*)stlp::RecordNamed(static_cast<stlp::RecordsByName*>(TableObject(table)), nullptr, name);
}
// push_back of the pointer vectors: 0069BD30 (the three item lists), 0069BDC0 (spells), 0069BDF0
// (modifiers); vector<StockMaterial>::_M_insert_overflow 0069CC20.

// The counts and the material values are read into locals the original reuses for every section,
// so a failed read leaves the previous value.
void ReadInt(void* stream, void* value) { static_cast<CStream*>(stream)->Read(value, 4u); }

// The next string of the stream into `name` (through a temporary, as the original).
void ReadName(void* stream, mfc::CString& name) {
    alignas(mfc::CString) unsigned char temporary[sizeof(mfc::CString)];
    mfc::CString* read = static_cast<CStream*>(stream)->ReadString(temporary);
    name = *read;
    read->~CString();
}

// std::find (0069E350..0069E3D0, one instantiation per list), then push_back when missing.
void AddUnique(StockPtrVector& list, char* record) {
    char** p = list.begin();
    while (p != list.end() && *p != record) ++p;
    if (p == list.end()) list.PushBack<PtrElement>(record);
}

// A record section: a count, then names lower-cased and looked up in `table`.
void LoadRecords(void* stream, mfc::CString& name, unsigned& count, const RecordTable& table, StockPtrVector& list) {
    ReadInt(stream, &count);
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        name.MakeLower();
        char* record = FindNamed(table, name);
        if (record) AddUnique(list, record);
    }
}

// A map section: a count, then names (as read) looked up in an STLport map by string.
void LoadMapped(void* stream, mfc::CString& name, unsigned& count, RecordNameMap& map, StockPtrVector& list) {
    ReadInt(stream, &count);
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        char* record;
        {
            stlp::string key(static_cast<const char*>(name));
            record = NamedRecord(map, key);
        }
        if (record) AddUnique(list, record);
    }
}

} // namespace

// Original behaviour kept: a material entry is pushed even when its material is not found (null
// record), and the two map lookups insert null entries for unknown names.
void ItemStock::LoadLists(void* stream) {
    PartyOf(this)->SpellManager();   // (the spell and item managers, fetched and not used)
    PartyOf(this)->ItemManager();
    mfc::CString name;
    unsigned count = 0;
    LoadRecords(stream, name, count, itemdb::Armors(), armors);
    LoadRecords(stream, name, count, itemdb::Weapons(), weapons);
    LoadRecords(stream, name, count, itemdb::QuickItems(), quickItems);

    ReadInt(stream, &count);
    int values[3] = {0, 0, 0};
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        name.MakeLower();
        StockMaterial entry;
        for (int& v : values) ReadInt(stream, &v);
        entry.record = static_cast<char*>(stlp::RecordNamed(static_cast<stlp::RecordsByName*>(TableObject(itemdb::Materials())), nullptr, name));
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

void SaveItemRecords(CStream* stream, const StockPtrVector& list) {
    WriteInt(stream, static_cast<std::uint32_t>(list.size()));
    for (char** p = list.begin(); p != list.end(); ++p)
        WriteText(stream, reinterpret_cast<const itemdb::ItemRecordHead*>(*p)->text04);
}

} // namespace

void ItemStock::SaveLists(void* stream) {
    auto* out = static_cast<CStream*>(stream);
    SaveItemRecords(out, armors);
    SaveItemRecords(out, weapons);
    SaveItemRecords(out, quickItems);

    WriteInt(out, static_cast<std::uint32_t>(materials.size()));
    for (StockMaterial* e = materials.begin(); e != materials.end(); ++e) {
        WriteText(out, reinterpret_cast<const itemdb::MaterialRecordHead*>(e->record)->key);
        WriteInt(out, static_cast<std::uint32_t>(e->minCount));
        WriteInt(out, static_cast<std::uint32_t>(e->maxCount));
        WriteInt(out, static_cast<std::uint32_t>(e->left));
    }

    WriteInt(out, static_cast<std::uint32_t>(spells.size()));
    for (char** p = spells.begin(); p != spells.end(); ++p)
        WriteText(out, reinterpret_cast<const itemdb::SpellRecord*>(*p)->code);

    // The modifiers' codes go through a CString made from a stack copy of their first four bytes
    // (the original's fifth byte is an uninitialised stack byte, its sixth a zero; ours stops
    // after the four).
    WriteInt(out, static_cast<std::uint32_t>(modifiers.size()));
    for (char** p = modifiers.begin(); p != modifiers.end(); ++p) {
        char code[5] = {};
        std::memcpy(code, reinterpret_cast<const itemdb::SpellModifierRecord*>(*p)->code, 4);
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


// A record's index in `table` (signed division), or -1 for null.
int TableIndex(const char* record, const RecordTable& table, int recordSize) {
    if (!record) return -1;
    std::intptr_t offset = reinterpret_cast<std::intptr_t>(record) - reinterpret_cast<std::intptr_t>(table.begin);
    return static_cast<int>(offset) / recordSize;
}

// The offered spells' and modifiers' records (from the maps by name) have the tables' layouts:
// the spell's code a CString at +4, the modifier's inline at +4.
int SpellIndex(char* record) {
    const char* code = reinterpret_cast<const itemdb::SpellRecord*>(record)->code;
    return TableIndex(static_cast<itemdb::CodeIndexedTable*>(TableObject(itemdb::Spells()))->FindByCode(code), itemdb::Spells(), itemdb::kSpellSize);
}
int ModifierIndex(char* record) {
    const char* code = Modifier(record)->code;
    return TableIndex(static_cast<itemdb::CodeIndexedTable*>(TableObject(itemdb::SpellModifiers()))->FindByCode(code), itemdb::SpellModifiers(),
                      itemdb::kSpellModifierSize);
}

// The common body of 00699C90 / 00699ED0: the source copied to its scratch vector (007C8670 spells,
// 007C8660 modifiers), then an item of kind 0x3007 with the class-0x15 prototype named at
// `prototypeName` per record, +0x54 its index, +0x48 a random count. The scratch is emptied at the end.
void GenerateMagicInstructions(ItemStock* stock, const StockPtrVector& source, StockPtrVector& taken, const char* prototypeName,
                               int (*indexOf)(char*), int minCount, int maxCount, StockList* out) {
    CItemConstructor* ctor = ConstructorOf(stock);
    void* manager = PartyOf(stock)->ItemManager();
    PartyOf(stock)->SpellManager();   // (fetched and not used)
    taken.Finish() = taken.begin();   // erase(begin, end)
    if (&source != &taken) AssignPointers(taken, source);
    char* prototype = static_cast<char*>(stlp::RecordNamed(static_cast<stlp::RecordsByName*>(TableObject(itemdb::Loot())), nullptr, prototypeName));
    for (char** p = taken.begin(); p != taken.end(); ++p) {
        ctor->SetManager(static_cast<CBaseManager*>(manager));
        ctor->BeginCreateItem(0x3007, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(nullptr);
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        static_cast<CItemLoot*>(item)->index_ = static_cast<std::uint32_t>(indexOf(*p));
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
    void* manager = PartyOf(this)->ItemManager();
    PartyOf(this)->SpellManager();   // (fetched and not used)
    FillSpellMaps(stock::ScrollSpellRecipes());
    StockList list;
    list.Construct();
    for (int tries = 0;;) {
        ClearSpellList(list);
        GenerateSpells(&list, scrollLevels, 0x32);
        unsigned count = 0;
        for (StockListNode* n = list.head->next; n != list.head; n = n->next) ++count;
        if (count >= static_cast<unsigned>(spells.size())) break;
        if (++tries >= kScrollTries) break;
    }
    char* prototype = FindNamed(itemdb::QuickItems(), game::SpellContainerText());
    for (StockListNode* n = list.head->next; n != list.head; n = n->next) {
        ctor->SetManager(static_cast<CBaseManager*>(manager));
        ctor->BeginCreateItem(0x3008, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(nullptr);
        ctor->SetSpellObject(static_cast<CSpell*>(n->data));
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

void AddUnlessPresent(StockPtrVector& list, char* record) {
    if (!record) return;
    char** p = list.begin();
    while (p != list.end() && *p != record) ++p;
    if (p == list.end()) list.push_back(record);  // grows as _M_insert_overflow does
}

} // namespace

void ItemStock::AddArmor(char* record) { AddUnlessPresent(armors, record); }
void ItemStock::AddWeapon(char* record) { AddUnlessPresent(weapons, record); }
void ItemStock::AddQuickItem(char* record) { AddUnlessPresent(quickItems, record); }
void ItemStock::AddSpell(char* record) { AddUnlessPresent(spells, record); }
void ItemStock::AddModifier(char* record) { AddUnlessPresent(modifiers, record); }
EI_REWRITTEN(0x00699080, ItemStock::AddArmor);
EI_REWRITTEN(0x006990E0, ItemStock::AddWeapon);
EI_REWRITTEN(0x00699140, ItemStock::AddQuickItem);
EI_REWRITTEN(0x006991A0, ItemStock::AddSpell);
EI_REWRITTEN(0x00699200, ItemStock::AddModifier);

void ItemStock::AddMaterial(char* record, int minCount, int maxCount) {
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
constinit stlp::vector<char*> g_takenPrototypes = /*007C8680*/ {};
constinit stlp::vector<StockMaterial*> g_candidates = /*007C8650*/ {};
constinit stlp::vector<StockMaterial> g_takenMaterials = /*007C8640*/ {};
constinit stlp::vector<char*> g_takenSpells = /*007C8670*/ {};
constinit stlp::vector<char*> g_takenModifiers = /*007C8660*/ {};
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
