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
#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_library.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// ---- Original code --------------------------------------------------------------------------------

// The item constructor, through its game addresses (item_constructor.cpp; the verify wrappers stay
// in the path), and 006A6480: whether the prototype and material allow a spell (not for kinds
// 0x11 / 0x12 without a material), with the level (prototype +0x44 + material +0x1C, as a float)
// and the power limit (prototype +0x40 + material +0x18). Sets only AL.
struct Constructor {
    void SetManager(void* manager);                 // 006A5C00
    int BeginCreateItem(int kind, int key);         // 006A5C50
    int SetMaterial(char* material);                // 006A5CC0
    int SetPrototype(char* prototype);              // 006A5CD0
    int SetSpellObject(void* spell);                // 006A5CE0
    CItem* EndCreateItem();                         // 006A5CF0
    int Cancel();                                   // 006A5D30
    bool MagicCapacity(float* level, float* power); // 006A6480

    std::uint8_t unknown_00[0x30];
    int count;                                      // +0x30 the stack size (CItemConstructor::count_)
};
__declspec(naked) void Constructor::SetManager(void*) EI_ORIGINAL(0x006A5C00)
__declspec(naked) int Constructor::BeginCreateItem(int, int) EI_ORIGINAL(0x006A5C50)
__declspec(naked) int Constructor::SetMaterial(char*) EI_ORIGINAL(0x006A5CC0)
__declspec(naked) int Constructor::SetPrototype(char*) EI_ORIGINAL(0x006A5CD0)
__declspec(naked) int Constructor::SetSpellObject(void*) EI_ORIGINAL(0x006A5CE0)
__declspec(naked) CItem* Constructor::EndCreateItem() EI_ORIGINAL(0x006A5CF0)
__declspec(naked) int Constructor::Cancel() EI_ORIGINAL(0x006A5D30)
__declspec(naked) bool Constructor::MagicCapacity(float*, float*) EI_ORIGINAL(0x006A6480)
static_assert(offsetof(Constructor, count) == offsetof(CItemConstructor, count_));

// 006A4230: an item's category (thiscall).
__declspec(naked) int __fastcall ItemCategory(CItem*) EI_ORIGINAL(0x006A4230)
// 0069D380: the class-0x15 prototype table's record by name (thiscall on 007C8778, item_db.h's
// QuestItems() table).
__declspec(naked) char* __fastcall FindTable15Named(void*, void*, const char*) EI_ORIGINAL(0x0069D380)
// 0069A480 (RET 4): fills the stock's spell and modifier maps from a spell table (007C8568 for
// weapons, 007C855C for armors). 0069A780 (RET 0xC): random spells into a list of counted spell
// pointers, from the maps, with the level per school (8 floats) and a percentage threshold.
__declspec(naked) void __fastcall CallFillSpellMaps(ItemStock*, void*, void* /*table*/) EI_ORIGINAL(0x0069A480)
__declspec(naked) void __fastcall CallGenerateSpells(ItemStock*, void*, StockList* /*out*/, const float* /*levels*/, unsigned /*threshold*/)
    EI_ORIGINAL(0x0069A780)

// vector<char*> members (thiscall): 0069E3F0 insert(position, first, last; a fourth unused
// argument), 0069D9A0 allocate a block of n and copy [first, last) into it, 0069D470
// _M_insert_overflow(position, &value, n); 0069C990 deallocate (fastcall: block, element count).
__declspec(naked) void __fastcall PtrInsertRange(StockPtrVector*, void*, char**, char**, char**, void*) EI_ORIGINAL(0x0069E3F0)
__declspec(naked) char** __fastcall PtrAllocateCopy(StockPtrVector*, void*, std::size_t, char**, char**) EI_ORIGINAL(0x0069D9A0)
__declspec(naked) void __fastcall PtrInsertOverflow(void*, void*, StockMaterial**, StockMaterial* const*, std::size_t)
    EI_ORIGINAL(0x0069D470)
__declspec(naked) void __fastcall PtrDeallocate(char**, std::size_t) EI_ORIGINAL(0x0069C990)
// vector<StockMaterial> members: 0069ECD0 insert(position, first, last; unused), 0069DB00
// allocate and copy, 0069C9D0 deallocate.
__declspec(naked) void __fastcall MaterialInsertRange(StockMaterialVector*, void*, StockMaterial*, StockMaterial*,
                                                      StockMaterial*, std::size_t) EI_ORIGINAL(0x0069ECD0)
__declspec(naked) StockMaterial* __fastcall MaterialAllocateCopy(StockMaterialVector*, void*, std::size_t, StockMaterial*,
                                                                 StockMaterial*) EI_ORIGINAL(0x0069DB00)
__declspec(naked) void __fastcall MaterialDeallocate(StockMaterial*, std::size_t) EI_ORIGINAL(0x0069C9D0)
// list<CItem*>::insert(position, value) (RET 0xC; the result iterator by hidden pointer) and the
// spell list's constructor (0069C440: the sentinel from the node pool; an allocator argument).
__declspec(naked) void __fastcall ListInsert(StockList*, void*, StockListNode** /*result*/, StockListNode* /*position*/,
                                             CItem* const* /*value*/) EI_ORIGINAL(0x0069C3D0)
__declspec(naked) void __fastcall SpellListConstruct(StockList*, void*, const void* /*allocator*/) EI_ORIGINAL(0x0069C440)

// ---- Globals (item_stock.h, namespace stock) -------------------------------------------------------

using stock::CandidateVector;
using stock::Candidates;
using stock::TakenMaterials;
using stock::TakenPrototypes;

IsaacRandom& GlobalRandom() { return IsaacRandom::Global(); }

// The record tables the original passes as `this` to their lookups.
void* TableObject(const RecordTable& table) { return const_cast<RecordTable*>(&table); }

// The template's party object (ItemStock::context): slot 16 the item constructor, 18 the item
// manager, 22 the spell manager.
CParty* PartyOf(const ItemStock* stock) { return static_cast<CParty*>(static_cast<CEngineObject*>(stock->context)); }
Constructor* ConstructorOf(const ItemStock* stock) { return reinterpret_cast<Constructor*>(PartyOf(stock)->ItemConstructor()); }

int Compare(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}
int CompareNoCase(const char* a, const char* b) {
    return gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}

// A float the original stores (FSTP dword) before using it again.
float Round(float value) {
    volatile float stored = value;
    return stored;
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

// vector<char*>::operator=, as the original inlines it. The spell and modifier vectors are other
// instantiations of the same code (allocate and copy 0069DC70 / 0069DDD0, deallocate 004AA810 /
// 004AA7D0); their copy helpers (0069EB10 / 0069EB40, 0069F560 / 0069F5C0) are the loops below.
using PtrAllocateCopyFn = char**(__fastcall*)(StockPtrVector*, void*, std::size_t, char**, char**);
using PtrDeallocateFn = void(__fastcall*)(char**, std::size_t);
void AssignPointers(StockPtrVector& to, const StockPtrVector& from, PtrAllocateCopyFn allocate = PtrAllocateCopy,
                    PtrDeallocateFn deallocate = PtrDeallocate) {
    std::size_t length = from.size();
    if (length > static_cast<std::size_t>(to.eos - to.start)) {
        char** block = allocate(&to, nullptr, length, from.start, from.finish);
        deallocate(to.start, static_cast<std::size_t>(to.eos - to.start));
        to.start = block;
        to.eos = block + length;
    } else if (to.size() >= length) {
        for (std::size_t i = 0; i < length; ++i) to.start[i] = from.start[i];
    } else {
        std::size_t size = to.size();
        for (std::size_t i = 0; i < size; ++i) to.start[i] = from.start[i];
        char** dest = to.finish;
        for (char** p = from.start + size; p != from.finish; ++p, ++dest) *dest = *p;
    }
    to.finish = to.start + length;
}

// vector<StockMaterial>::operator=, as the original inlines it.
void AssignMaterials(StockMaterialVector& to, const StockMaterialVector& from) {
    std::size_t length = from.size();
    if (length > static_cast<std::size_t>(to.eos - to.start)) {
        StockMaterial* block = MaterialAllocateCopy(&to, nullptr, length, from.start, from.finish);
        MaterialDeallocate(to.start, static_cast<std::size_t>(to.eos - to.start));
        to.start = block;
        to.eos = block + length;
    } else if (to.size() >= length) {
        for (std::size_t i = 0; i < length; ++i) to.start[i] = from.start[i];
    } else {
        std::size_t size = to.size();
        for (std::size_t i = 0; i < size; ++i) to.start[i] = from.start[i];
        StockMaterial* dest = to.finish;
        for (StockMaterial* p = from.start + size; p != from.finish; ++p, ++dest) *dest = *p;
    }
    to.finish = to.start + length;
}

// Appends an item to the output list (counted by the caller).
void Output(StockList* out, CItem* item) {
    StockListNode* result;
    ListInsert(out, nullptr, &result, out->head, &item);
}

// The item's reference count.
void AddReference(CItem* item) { ++item->refCount_; }

// A prototype record: text08 its name, text10 its material class, price, unknown_4C the material
// units it takes; record slot 0 the item kind.
itemdb::ItemRecordHead* Record(char* p) { return reinterpret_cast<itemdb::ItemRecordHead*>(p); }
const char* PrototypeName(char* p) { return Record(p)->text08; }
const char* MaterialClass(char* p) { return Record(p)->text10; }
int MaterialUnits(char* p) { return static_cast<int>(Record(p)->unknown_4C); }
const itemdb::MaterialRecordHead* Material(const StockMaterial* e) {
    return reinterpret_cast<const itemdb::MaterialRecordHead*>(e->record);
}

// Picks the material of a prototype from the candidates (pointers into the stock's entries). Each
// candidate in turn is taken with a chance by its quality q (material +0x10) against the
// prototype's price p: 10% outside [2p/7 - p/5, p/5], else 110 - |(q - p/7) / (p/7 - p/5)| x 100
// percent (truncated). Null when none was taken.
StockMaterial* DrawMaterial(char* prototype) {
    float seventh = Round(Record(prototype)->price * stock::SeventhFactor());
    float fifth = Round(Record(prototype)->price * stock::FifthFactor());
    CandidateVector& c = Candidates();
    if (c.start == c.finish) return nullptr;
    float low = Round((seventh + seventh) - fifth);
    for (StockMaterial** q = c.start; q != c.finish; ++q) {
        int chance = 10;
        float quality = Material(*q)->quality;
        if (quality >= low && !(quality > fifth)) {
            float t = Round(((quality - seventh) / (seventh - fifth)) * 100.0f);
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
void AddSpell(ItemStock* stock, Constructor* ctor, StockList& spells) {
    if (GlobalRandom().Next() % 101 >= 50) return;
    float level = 0.0f;
    float power = 0.0f;
    if (!ctor->MagicCapacity(&level, &power)) return;
    float levels[8];
    for (float& l : levels) l = level;
    CallGenerateSpells(stock, nullptr, &spells, levels, 0);
    unsigned count = 0;
    for (StockListNode* n = spells.head->next; n != spells.head; n = n->next) ++count;
    if (count == 0) return;
    unsigned index = count > 1 ? GlobalRandom().Next() % count : 0;
    StockListNode* node = spells.head->next;
    for (unsigned i = 0; i < index; ++i) node = node->next;
    void* spell = node->data;
    float spellPower;   // CSpell +0x40, the prototype's +0x18 as float bits
    std::memcpy(&spellPower, &static_cast<CSpell*>(static_cast<CEngineObject*>(spell))->unknown_40, 4);
    if (!(spellPower > power)) ctor->SetSpellObject(spell);
}

} // namespace

// ---- 0069AE20 -------------------------------------------------------------------------------------

// Original bug kept: a failed build (EndCreateItem null) is dereferenced.
void ItemStock::GenerateItems(StockPtrVector* prototypes, int minCount, int maxCount, StockList* out, int kind) {
    Constructor* ctor = ConstructorOf(this);
    void* manager = PartyOf(this)->ItemManager();
    if (kind == 0 || kind == 3)
        CallFillSpellMaps(this, nullptr, stock::WeaponSpellRecipes());
    else if (kind == 1)
        CallFillSpellMaps(this, nullptr, stock::ArmorSpellRecipes());

    int count = RandomCount(minCount, maxCount);
    StockPtrVector& taken = TakenPrototypes();
    taken.finish = taken.start;   // erase(begin, end)
    if (prototypes->size() > static_cast<std::size_t>(count)) {
        Shuffle(prototypes->start, prototypes->finish);
        PtrInsertRange(&taken, nullptr, taken.start, prototypes->start, prototypes->start + count, prototypes);
    } else if (prototypes != &taken) {
        AssignPointers(taken, *prototypes);
    }

    CandidateVector& candidates = Candidates();
    for (char** p = taken.start; p != taken.finish; ++p) {
        char* prototype = *p;
        if (Compare(PrototypeName(prototype), game::WandText()) == 0) continue;
        ctor->SetManager(manager);
        ctor->BeginCreateItem(itemdb::RecordSlots(Record(prototype))->Kind(), -1);
        ctor->SetPrototype(prototype);
        candidates.finish = candidates.start;   // erase(begin, end)
        if (CompareNoCase(MaterialClass(prototype), game::NoneText()) != 0) {
            for (StockMaterial* e = materials.start; e != materials.finish; ++e) {
                if (Compare(MaterialClass(prototype), Material(e)->soundName) != 0) continue;
                if (MaterialUnits(prototype) > e->left) continue;
                if (candidates.finish != candidates.eos) {
                    if (candidates.finish) *candidates.finish = e;
                    ++candidates.finish;
                } else {
                    PtrInsertOverflow(&candidates, nullptr, candidates.finish, &e, 1);
                }
            }
        }
        StockMaterial* chosen = DrawMaterial(prototype);
        if (!chosen) {
            if (CompareNoCase(MaterialClass(prototype), game::NoneText()) != 0) {
                if (candidates.finish == candidates.start) {
                    ctor->Cancel();
                    ctor->SetManager(nullptr);
                    continue;
                }
                Shuffle(candidates.start, candidates.finish);
                chosen = *candidates.start;
            }
        }
        if (chosen) {
            chosen->left -= MaterialUnits(prototype);
            ctor->SetMaterial(chosen->record);
        } else {
            ctor->SetMaterial(nullptr);
        }

        StockList spellList;
        char allocator = 0;
        SpellListConstruct(&spellList, nullptr, &allocator);
        if (kind == 1 || kind == 3 || kind == 0) ::AddSpell(this, ctor, spellList);
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        AddReference(item);
        if (item->type_ == 0x3006 && ItemCategory(item) == 8)
            item->unknown_48 = static_cast<std::uint32_t>(gamecrt::rand() * 30 / 0x7FFF % 30 + 20);
        Output(out, item);
        DestroySpellList(spellList);
    }
    candidates.finish = candidates.start;
}
EI_REPLACE_METHOD(0x0069AE20, ItemStock::GenerateItems);

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
    Constructor* ctor = ConstructorOf(this);
    void* manager = PartyOf(this)->ItemManager();
    int count = RandomCount(minCount, maxCount);
    StockMaterialVector& taken = TakenMaterials();
    taken.finish = taken.start;   // erase(begin, end)
    if (materials.size() > static_cast<std::size_t>(count)) {
        Shuffle(materials.start, materials.finish);
        MaterialInsertRange(&taken, nullptr, taken.start, materials.start, materials.start + count,
                            static_cast<std::size_t>(count));
    } else if (&materials != &taken) {
        AssignMaterials(taken, materials);
    }
    char* prototype = FindTable15Named(TableObject(itemdb::QuestItems()), nullptr, game::MaterialText());
    for (StockMaterial* e = taken.start; e != taken.finish; ++e) {
        ctor->SetManager(manager);
        ctor->BeginCreateItem(0x3007, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(e->record);
        int range = e->maxCount - e->minCount + 1;
        ctor->count = gamecrt::rand() * range / 0x7FFF % range + e->minCount;
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        AddReference(item);
        Output(out, item);
    }
    taken.finish = taken.start;
}
EI_REPLACE_METHOD(0x0069A110, ItemStock::GenerateMaterials);

// ---- 00699810 / 006995E0 / 00699A40 ---------------------------------------------------------------

namespace {

// The common body of the three instruction generators: the source records are copied to the
// scratch 007C8680, then each (except one named `skip`) becomes an item of kind 0x3007 with the
// class-0x15 prototype named at `prototypeName`, no material, +0x54 the record's index in the table
// `table` (records of `recordSize` bytes) and +0x48 a random count.
void GenerateInstructions(ItemStock* stock, const StockPtrVector& source, const char* prototypeName, const RecordTable& table,
                          int recordSize, const char* skip, int minCount, int maxCount, StockList* out) {
    Constructor* ctor = ConstructorOf(stock);
    void* manager = PartyOf(stock)->ItemManager();
    StockPtrVector& taken = TakenPrototypes();
    taken.finish = taken.start;   // erase(begin, end)
    if (&source != &taken) AssignPointers(taken, source);
    char* prototype = FindTable15Named(TableObject(itemdb::QuestItems()), nullptr, prototypeName);
    for (char** p = taken.start; p != taken.finish; ++p) {
        if (skip && Compare(PrototypeName(*p), skip) == 0) continue;
        ctor->SetManager(manager);
        ctor->BeginCreateItem(0x3007, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(nullptr);
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        std::intptr_t offset = reinterpret_cast<std::intptr_t>(*p) - reinterpret_cast<std::intptr_t>(table.begin);
        static_cast<CItemLoot*>(item)->index_ = static_cast<std::uint32_t>(static_cast<int>(offset) / recordSize);
        item->unknown_48 = static_cast<std::uint32_t>(RandomCount(minCount, maxCount));
        AddReference(item);
        Output(out, item);
    }
    taken.finish = taken.start;
}

} // namespace

void ItemStock::GenerateWeaponInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, weapons, game::InstructionWeaponText(), itemdb::Weapons(), itemdb::kWeaponSize, nullptr,
                         minCount, maxCount, out);
}
EI_REPLACE_METHOD(0x00699810, ItemStock::GenerateWeaponInstructions);

void ItemStock::GenerateArmorInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, armors, game::InstructionArmorText(), itemdb::Armors(), itemdb::kArmorSize, nullptr, minCount,
                         maxCount, out);
}
EI_REPLACE_METHOD(0x006995E0, ItemStock::GenerateArmorInstructions);

// Quick items named "scroll" get no instruction.
void ItemStock::GenerateQuickItemInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, quickItems, game::InstructionQuickItemText(), itemdb::QuickItems(), itemdb::kQuickItemSize,
                         game::ScrollText(), minCount, maxCount, out);
}
EI_REPLACE_METHOD(0x00699A40, ItemStock::GenerateQuickItemInstructions);

// ---- 0069A480 / 0069A780 --------------------------------------------------------------------------

namespace {

// The spell recipe hash_map (+0x50, STLport hash_map<string, list<recipe*>>): clear 004AAEB0,
// find-or-insert of a {name, list} pair 0069D130 (returns the node's pair: the list at +0xC),
// begin 0069C160 (RET 4: the iterator by hidden pointer) and the first node of the next non-empty
// bucket 0069D0E0 (thiscall on the iterator, still on the last node). Its list: constructor
// 0069C680 (an allocator argument), destructor 0069C830, end 0069C2E0 (RET 4: the iterator by
// hidden pointer), insert 0069C2F0 (result, position, &value); the pair: constructor 0069C1C0
// (the record, whose name starts it, and &list), destructor 004AD640.
__declspec(naked) void __fastcall RecipeMapClear(void*) EI_ORIGINAL(0x004AAEB0)
__declspec(naked) StockRecipeEntry* __fastcall RecipeMapInsert(void*, void*, const void* /*pair*/) EI_ORIGINAL(0x0069D130)
struct RecipeIterator {
    StockRecipeNode* node;
    void* map;
};
__declspec(naked) void __fastcall RecipeMapBegin(void*, void*, RecipeIterator*) EI_ORIGINAL(0x0069C160)
__declspec(naked) StockRecipeNode* __fastcall RecipeMapNextBucket(RecipeIterator*) EI_ORIGINAL(0x0069D0E0)
__declspec(naked) void __fastcall RecipeListConstruct(StockList*, void*, const void*) EI_ORIGINAL(0x0069C680)
__declspec(naked) void __fastcall RecipeListDestroy(StockList*) EI_ORIGINAL(0x0069C830)
__declspec(naked) void __fastcall RecipeListEnd(StockList*, void*, StockListNode**) EI_ORIGINAL(0x0069C2E0)
__declspec(naked) void __fastcall RecipeListInsert(StockList*, void*, StockListNode**, StockListNode*, char* const*)
    EI_ORIGINAL(0x0069C2F0)
__declspec(naked) void __fastcall RecipePairConstruct(void*, void*, const char* /*record*/, const StockList*)
    EI_ORIGINAL(0x0069C1C0)
__declspec(naked) void __fastcall RecipePairDestroy(void*) EI_ORIGINAL(0x004AD640)
// The modifier map (+0x64, STLport map<string, modifier*>): clear 004AB2B0, operator[] with a
// {key, null} pair 0069D1E0 (returns the node's pair: the value at +0xC).
__declspec(naked) void __fastcall ModifierMapClear(void*) EI_ORIGINAL(0x004AB2B0)
__declspec(naked) StockModifierEntry* __fastcall ModifierMapAt(void*, void*, const void* /*pair*/) EI_ORIGINAL(0x0069D1E0)
// The spell records by name (map 007C851C, stock::SpellsByName; operator[] 004ABD00: inserts a
// null for a new name).
char** __fastcall SpellByName(void* a0, void*, const void* a2) { return (char**)SpellMapAtOriginal(a0, nullptr, a2); }
// The spell prototypes by name (thiscall on 007C8510, 00613920) and the spell manager's builder
// (00689310, RET 8: prototype, modifier list; a new counted spell or null).
itemdb::SpellRecord* FindSpellPrototype(const char* name) {
    return reinterpret_cast<itemdb::SpellRecord*>(SpellPrototypeByNameOriginal(TableObject(itemdb::Spells()), nullptr, name));
}
__declspec(naked) CEngineObject* __fastcall BuildSpell(void*, void*, itemdb::SpellRecord* /*prototype*/, StockList* /*modifiers*/)
    EI_ORIGINAL(0x00689310)
// The modifier list (list<modifier*>): constructor 0069BF70 (an allocator argument), end 0068C500,
// insert 0068C560 (result, position, &value), node free 0068C640 (fastcall: node, count).
__declspec(naked) void __fastcall ModifierListConstruct(StockList*, void*, const void*) EI_ORIGINAL(0x0069BF70)
__declspec(naked) void __fastcall ModifierListEnd(StockList*, void*, StockListNode**) EI_ORIGINAL(0x0068C500)
void __fastcall ModifierListInsert(StockList* a0, void*, StockListNode** a2, StockListNode* a3, char* const* a4) { ListInsert68C560Original(a0, nullptr, (void**)a2, a3, a4); }
__declspec(naked) void __fastcall ModifierNodesFree(StockListNode*, int) EI_ORIGINAL(0x0068C640)
// The output list of counted spells: end 0069BE60, insert 0069C410 (result, position, &value).
__declspec(naked) void __fastcall SpellListEnd(StockList*, void*, StockListNode**) EI_ORIGINAL(0x0069BE60)
__declspec(naked) void __fastcall SpellListInsert(StockList*, void*, StockListNode**, StockListNode*, CEngineObject* const*)
    EI_ORIGINAL(0x0069C410)

// The modifier map's lookup pair: a copied key and a null value (0x10 bytes, a StockModifierEntry).
struct ModifierKey {
    stlp::string key;
    char* value = nullptr;
    explicit ModifierKey(const stlp::string& s) : key(s) {}
};
static_assert(sizeof(ModifierKey) == sizeof(StockModifierEntry));

// The modifier map's entry for `name` (created null when missing, as operator[] does).
char*& ModifierSlot(ItemStock* stock, const stlp::string& name) {
    ModifierKey pair(name);
    return ModifierMapAt(stock->modifierMap, nullptr, &pair)->modifier;
}

// A modifier record (itemdb::SpellModifierRecord): its code's first letter the kind, mana the cost
// added, strength (a cost factor for kind 'm'), skill the level it adds.
const itemdb::SpellModifierRecord* Modifier(char* m) { return reinterpret_cast<const itemdb::SpellModifierRecord*>(m); }
char ModifierKind(char* m) { return Modifier(m)->code[0]; }
float ModifierCost(char* m) { return Modifier(m)->mana; }
float ModifierFactor(char* m) { return Modifier(m)->strength; }
int ModifierLevel(char* m) { return Modifier(m)->skill; }

void AppendModifier(StockList& list, char* modifier) {
    StockListNode* end;
    ModifierListEnd(&list, nullptr, &end);
    StockListNode* result;
    ModifierListInsert(&list, nullptr, &result, end, &modifier);
}

void ClearModifierList(StockList& list) {
    StockListNode* node = list.head->next;
    while (node != list.head) {
        StockListNode* next = node->next;
        ModifierNodesFree(node, 1);
        node = next;
    }
    list.head->next = list.head;
    list.head->prev = list.head;
}

} // namespace

// Original bug kept: the 004ABD00 lookup inserts a null entry into 007C851C for every recipe name
// it does not know.
void ItemStock::FillSpellMaps(const SpellRecipeVector* recipes) {
    RecipeMapClear(spellMap);
    ModifierMapClear(modifierMap);
    for (char* r = recipes->start; r != recipes->finish; r += kSpellRecipeSize) {
        if (!(reinterpret_cast<const SpellRecipe*>(r)->classMask & (1u << (classIndex & 31)))) continue;
        char* spell = *SpellByName(stock::SpellsByName(), nullptr, r);   // keyed by the recipe's spell name (+0)
        if (!spell) continue;
        bool offered = false;
        for (char** s = spells.start; s != spells.finish; ++s) {
            if (*s == spell) {
                offered = true;
                break;
            }
        }
        if (!offered) continue;
        char* value = r;
        StockList list;
        char allocator = 0;
        RecipeListConstruct(&list, nullptr, &allocator);
        alignas(4) std::uint8_t pair[0x10];
        RecipePairConstruct(pair, nullptr, r, &list);
        StockRecipeEntry* inserted = RecipeMapInsert(spellMap, nullptr, pair);
        RecipePairDestroy(pair);
        RecipeListDestroy(&list);
        StockList* entries = &inserted->recipes;
        StockListNode* end;
        RecipeListEnd(entries, nullptr, &end);
        StockListNode* result;
        RecipeListInsert(entries, nullptr, &result, end, &value);
    }

    for (char** p = modifiers.start; p != modifiers.finish; ++p) {
        char* m = *p;
        char letters[2] = {Modifier(m)->code[0], Modifier(m)->code[1]};
        switch (ModifierKind(m)) {
        case 'a': case 'd': case 'e': case 'm': case 'r': case 't': {
            stlp::string key(letters, letters + 1);
            char* held = ModifierSlot(this, key);
            // FCOMP held+0x14 with m+0x14, TEST AH,1: replaced when less or unordered.
            if (!held || !(ModifierFactor(held) >= ModifierFactor(m))) ModifierSlot(this, key) = m;
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
EI_REPLACE_METHOD(0x0069A480, ItemStock::FillSpellMaps);

void ItemStock::GenerateSpells(StockList* out, const float* levels, unsigned threshold) {
    void* spellManager = PartyOf(this)->SpellManager();
    StockList chosen;
    char allocator = 0;
    ModifierListConstruct(&chosen, nullptr, &allocator);
    RecipeIterator it;
    RecipeMapBegin(spellMap, nullptr, &it);
    while (it.node) {
        StockListNode* head = it.node->entry.recipes.head;
        for (StockListNode* n = head->next; n != head; n = n->next) {
            if (GlobalRandom().Next() % 101 < threshold) continue;
            ClearModifierList(chosen);
            int count = 0;
            auto* recipe = static_cast<const SpellRecipe*>(n->data);
            itemdb::SpellRecord* prototype = FindSpellPrototype(recipe->spell.data());
            int level = prototype->skill;
            int budget = x87::fistp(levels[prototype->school]);
            float cost = prototype->mana;
            const StockStringVector& required = recipe->required;
            for (unsigned i = 0; i < required.size(); ++i) {
                char* m = ModifierSlot(this, required.start[i]);
                if (!m) continue;
                AppendModifier(chosen, m);
                cost = Round(cost + ModifierCost(m));
                ++count;
                level += ModifierLevel(m);
            }
            if (budget < level || count > 8) continue;
            const StockStringVector& optional = recipe->optional;
            for (unsigned i = 0; i < optional.size() && count < 8; ++i) {
                if (GlobalRandom().Next() % 101 < 50) continue;
                char* m = ModifierSlot(this, optional.start[i]);
                if (!m || budget < ModifierLevel(m) + level) continue;
                AppendModifier(chosen, m);
                cost = Round(cost + ModifierCost(m));
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
                        cost = Round(ModifierFactor(repeated) * prototype->mana + cost + ModifierCost(repeated));
                        if (!(cost > prototype->mana)) break;
                    } else {
                        cost = Round(cost + ModifierCost(repeated));
                    }
                    if (budget < ModifierLevel(repeated) + reached) break;
                }
            }
            CEngineObject* spell = BuildSpell(spellManager, nullptr, prototype, &chosen);
            if (spell) ++spell->refCount_;
            StockListNode* end;
            SpellListEnd(out, nullptr, &end);
            StockListNode* result;
            SpellListInsert(out, nullptr, &result, end, &spell);
            if (spell) spell->Release();   // slot 4
        }
        StockRecipeNode* next = it.node->next;
        it.node = next ? next : RecipeMapNextBucket(&it);
    }
    ClearModifierList(chosen);
    if (chosen.head) stlp::deallocate(chosen.head, sizeof(StockListNode));
}
EI_REPLACE_METHOD(0x0069A780, ItemStock::GenerateSpells);

// ---- 0069B5B0 -------------------------------------------------------------------------------------

namespace {

// CStream::ReadString (00461190): a CString constructed at `out` from the bytes up to a zero; the
// stream's Read is virtual slot 1.
__declspec(naked) mfc::CString* __fastcall StreamReadString(void*, void*, void* /*out*/) EI_ORIGINAL(0x00461190)
// The record lookups by name (thiscall on the tables; null when missing): armors 004AB820 on
// 007C8718, weapons 004AB720 on 007C86F8, quick items 00613820 on 007C8738, materials 0069D290 on
// 007C86D8; the spell and modifier maps' operator[] (004ABD00 on 007C851C, 00684B20 on 007C853C;
// they insert a null for a new name).
char* __fastcall FindArmorNamed(void* a0, void*, const char* a2) { return (char*)FindArmorOriginal(a0, nullptr, a2); }
char* __fastcall FindWeaponNamed(void* a0, void*, const char* a2) { return (char*)FindWeaponOriginal(a0, nullptr, a2); }
char* __fastcall FindQuickItemNamed(void* a0, void*, const char* a2) { return (char*)FindQuickItemOriginal(a0, nullptr, a2); }
__declspec(naked) char* __fastcall FindMaterialNamed(void*, void*, const char*) EI_ORIGINAL(0x0069D290)
__declspec(naked) char** __fastcall ModifierByName(void*, void*, const void* /*string*/) EI_ORIGINAL(0x00684B20)
// push_back of the pointer vectors (thiscall, &value): 0069BD30 (the three item lists), 0069BDC0
// (spells), 0069BDF0 (modifiers); vector<StockMaterial>::_M_insert_overflow 0069CC20 (position,
// &value, count).
__declspec(naked) void __fastcall PushItemRecord(StockPtrVector*, void*, char* const*) EI_ORIGINAL(0x0069BD30)
__declspec(naked) void __fastcall PushSpellRecord(StockPtrVector*, void*, char* const*) EI_ORIGINAL(0x0069BDC0)
__declspec(naked) void __fastcall PushModifierRecord(StockPtrVector*, void*, char* const*) EI_ORIGINAL(0x0069BDF0)
__declspec(naked) void __fastcall MaterialInsertOverflow(StockMaterialVector*, void*, StockMaterial*, const StockMaterial*,
                                                         std::size_t) EI_ORIGINAL(0x0069CC20)

using PushFn = void(__fastcall*)(StockPtrVector*, void*, char* const*);
using FindNamedFn = char*(__fastcall*)(void*, void*, const char*);
using MapAtFn = char**(__fastcall*)(void*, void*, const void*);

// The counts and the material values are read into locals the original reuses for every section,
// so a failed read leaves the previous value.
void ReadInt(void* stream, void* value) { static_cast<CStream*>(stream)->Read(value, 4u); }

// The next string of the stream into `name` (through a temporary, as the original).
void ReadName(void* stream, mfc::CString& name) {
    alignas(mfc::CString) unsigned char temporary[sizeof(mfc::CString)];
    mfc::CString* read = StreamReadString(stream, nullptr, temporary);
    name = *read;
    read->~CString();
}

// std::find (0069E350..0069E3D0, one instantiation per list), then push_back when missing.
void AddUnique(StockPtrVector& list, char* record, PushFn push) {
    char** p = list.start;
    while (p != list.finish && *p != record) ++p;
    if (p == list.finish) push(&list, nullptr, &record);
}

// A record section: a count, then names lower-cased and looked up in `table`.
void LoadRecords(void* stream, mfc::CString& name, unsigned& count, const RecordTable& table, FindNamedFn find,
                 StockPtrVector& list) {
    ReadInt(stream, &count);
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        name.MakeLower();
        char* record = find(TableObject(table), nullptr, name);
        if (record) AddUnique(list, record, &PushItemRecord);
    }
}

// A map section: a count, then names (as read) looked up in an STLport map by string.
void LoadMapped(void* stream, mfc::CString& name, unsigned& count, void* map, MapAtFn at, StockPtrVector& list,
                PushFn push) {
    ReadInt(stream, &count);
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        char* record;
        {
            stlp::string key(static_cast<const char*>(name));
            record = *at(map, nullptr, &key);
        }
        if (record) AddUnique(list, record, push);
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
    LoadRecords(stream, name, count, itemdb::Armors(), &FindArmorNamed, armors);
    LoadRecords(stream, name, count, itemdb::Weapons(), &FindWeaponNamed, weapons);
    LoadRecords(stream, name, count, itemdb::QuickItems(), &FindQuickItemNamed, quickItems);

    ReadInt(stream, &count);
    int values[3] = {0, 0, 0};
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        name.MakeLower();
        StockMaterial entry;
        for (int& v : values) ReadInt(stream, &v);
        entry.record = FindMaterialNamed(TableObject(itemdb::Materials()), nullptr, name);
        entry.minCount = values[0];
        entry.maxCount = values[1];
        entry.left = values[2];
        if (materials.finish != materials.eos) {
            if (materials.finish) *materials.finish = entry;
            ++materials.finish;
        } else {
            MaterialInsertOverflow(&materials, nullptr, materials.finish, &entry, 1);
        }
    }

    LoadMapped(stream, name, count, stock::SpellsByName(), &SpellByName, spells, &PushSpellRecord);
    LoadMapped(stream, name, count, stock::ModifiersByName(), &ModifierByName, modifiers, &PushModifierRecord);
}
EI_REPLACE_METHOD(0x0069B5B0, ItemStock::LoadLists);

// ---- 0069BAC0 (A-H17) ------------------------------------------------------------------------------

namespace {

void WriteInt(CStream* stream, std::uint32_t value) { stream->Write(&value, 4u); }
// A text with its terminating zero.
void WriteText(CStream* stream, const char* text) {
    stream->Write(text, static_cast<std::uint32_t>(std::strlen(text) + 1));
}

void SaveItemRecords(CStream* stream, const StockPtrVector& list) {
    WriteInt(stream, static_cast<std::uint32_t>(list.size()));
    for (char** p = list.start; p != list.finish; ++p)
        WriteText(stream, reinterpret_cast<const itemdb::ItemRecordHead*>(*p)->text04);
}

} // namespace

void ItemStock::SaveLists(void* stream) {
    auto* out = static_cast<CStream*>(stream);
    SaveItemRecords(out, armors);
    SaveItemRecords(out, weapons);
    SaveItemRecords(out, quickItems);

    WriteInt(out, static_cast<std::uint32_t>(materials.size()));
    for (StockMaterial* e = materials.start; e != materials.finish; ++e) {
        WriteText(out, reinterpret_cast<const itemdb::MaterialRecordHead*>(e->record)->key);
        WriteInt(out, static_cast<std::uint32_t>(e->minCount));
        WriteInt(out, static_cast<std::uint32_t>(e->maxCount));
        WriteInt(out, static_cast<std::uint32_t>(e->left));
    }

    WriteInt(out, static_cast<std::uint32_t>(spells.size()));
    for (char** p = spells.start; p != spells.finish; ++p)
        WriteText(out, reinterpret_cast<const itemdb::SpellRecord*>(*p)->code);

    // The modifiers' codes go through a CString made from a stack copy of their first four bytes
    // (the original's fifth byte is an uninitialised stack byte, its sixth a zero; ours stops
    // after the four).
    WriteInt(out, static_cast<std::uint32_t>(modifiers.size()));
    for (char** p = modifiers.start; p != modifiers.finish; ++p) {
        char code[5] = {};
        std::memcpy(code, reinterpret_cast<const itemdb::SpellModifierRecord*>(*p)->code, 4);
        mfc::CString text(code);
        WriteText(out, text);
    }
}
EI_REPLACE_METHOD(0x0069BAC0, ItemStock::SaveLists);

// ---- 006993B0 / 00699440 --------------------------------------------------------------------------

void ItemStock::ResetMaterialUnits() {
    for (StockMaterial* e = materials.start; e != materials.finish; ++e) {
        e->left = e->maxCount;
        if (e->maxCount > e->minCount) e->left = RandomCount(e->minCount, e->maxCount);
    }
}
EI_REPLACE_METHOD(0x006993B0, ItemStock::ResetMaterialUnits);

namespace {

// The spell and modifier vectors' allocate-and-copy (thiscall: count, first, last) and deallocate
// (fastcall: block, element count).
__declspec(naked) char** __fastcall SpellPtrAllocateCopy(StockPtrVector*, void*, std::size_t, char**, char**)
    EI_ORIGINAL(0x0069DC70)
__declspec(naked) void __fastcall SpellPtrDeallocate(char**, std::size_t) EI_ORIGINAL(0x004AA810)
__declspec(naked) char** __fastcall ModifierPtrAllocateCopy(StockPtrVector*, void*, std::size_t, char**, char**)
    EI_ORIGINAL(0x0069DDD0)
__declspec(naked) void __fastcall ModifierPtrDeallocate(char**, std::size_t) EI_ORIGINAL(0x004AA7D0)

} // namespace

void ItemStock::CopyMagic(const ItemStock* other) {
    if (&other->spells != &spells) AssignPointers(spells, other->spells, SpellPtrAllocateCopy, SpellPtrDeallocate);
    if (&other->modifiers != &modifiers)
        AssignPointers(modifiers, other->modifiers, ModifierPtrAllocateCopy, ModifierPtrDeallocate);
}
EI_REPLACE_METHOD(0x00699440, ItemStock::CopyMagic);

// ---- 00699C90 / 00699ED0 --------------------------------------------------------------------------

namespace {

// The spell and modifier records by code (thiscall on their tables; null when missing): 0068C820 on
// 007C8510 (0x84-byte records, the code a CString at +4), 0068C920 on 007C8530 (0x24-byte records,
// the code inline at +4).
__declspec(naked) char* __fastcall FindSpellByCode(void*, void*, const char*) EI_ORIGINAL(0x0068C820)
__declspec(naked) char* __fastcall FindModifierByCode(void*, void*, const char*) EI_ORIGINAL(0x0068C920)

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
    return TableIndex(FindSpellByCode(TableObject(itemdb::Spells()), nullptr, code), itemdb::Spells(), itemdb::kSpellSize);
}
int ModifierIndex(char* record) {
    const char* code = Modifier(record)->code;
    return TableIndex(FindModifierByCode(TableObject(itemdb::SpellModifiers()), nullptr, code), itemdb::SpellModifiers(),
                      itemdb::kSpellModifierSize);
}

// The common body of 00699C90 / 00699ED0: the source copied to its scratch vector (007C8670 spells,
// 007C8660 modifiers), then an item of kind 0x3007 with the class-0x15 prototype named at
// `prototypeName` per record, +0x54 its index, +0x48 a random count. The scratch is emptied at the end.
void GenerateMagicInstructions(ItemStock* stock, const StockPtrVector& source, StockPtrVector& taken,
                               PtrAllocateCopyFn allocate, PtrDeallocateFn deallocate, const char* prototypeName,
                               int (*indexOf)(char*), int minCount, int maxCount, StockList* out) {
    Constructor* ctor = ConstructorOf(stock);
    void* manager = PartyOf(stock)->ItemManager();
    PartyOf(stock)->SpellManager();   // (fetched and not used)
    taken.finish = taken.start;   // erase(begin, end)
    if (&source != &taken) AssignPointers(taken, source, allocate, deallocate);
    char* prototype = FindTable15Named(TableObject(itemdb::QuestItems()), nullptr, prototypeName);
    for (char** p = taken.start; p != taken.finish; ++p) {
        ctor->SetManager(manager);
        ctor->BeginCreateItem(0x3007, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(nullptr);
        CItem* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        static_cast<CItemLoot*>(item)->index_ = static_cast<std::uint32_t>(indexOf(*p));
        item->unknown_48 = static_cast<std::uint32_t>(RandomCount(minCount, maxCount));
        AddReference(item);
        Output(out, item);
    }
    taken.finish = taken.start;
}

} // namespace

void ItemStock::GenerateSpellInstructions(int minCount, int maxCount, StockList* out) {
    GenerateMagicInstructions(this, spells, stock::TakenSpells(), SpellPtrAllocateCopy, SpellPtrDeallocate,
                              game::PrototypeText(), SpellIndex, minCount, maxCount, out);
}
EI_REPLACE_METHOD(0x00699C90, ItemStock::GenerateSpellInstructions);

void ItemStock::GenerateModifierInstructions(int minCount, int maxCount, StockList* out) {
    GenerateMagicInstructions(this, modifiers, stock::TakenModifiers(), ModifierPtrAllocateCopy, ModifierPtrDeallocate,
                              game::RuneText(), ModifierIndex, minCount, maxCount, out);
}
EI_REPLACE_METHOD(0x00699ED0, ItemStock::GenerateModifierInstructions);

// ---- 0069AC20 -------------------------------------------------------------------------------------

namespace {
constexpr int kScrollTries = 10;
} // namespace

// Original bug kept: a failed build (EndCreateItem null) is dereferenced.
void ItemStock::GenerateScrolls(int /*minCount*/, int /*maxCount*/, StockList* out) {
    Constructor* ctor = ConstructorOf(this);
    void* manager = PartyOf(this)->ItemManager();
    PartyOf(this)->SpellManager();   // (fetched and not used)
    CallFillSpellMaps(this, nullptr, stock::ScrollSpellRecipes());
    StockList list;
    char allocator = 0;
    SpellListConstruct(&list, nullptr, &allocator);
    for (int tries = 0;;) {
        ClearSpellList(list);
        CallGenerateSpells(this, nullptr, &list, scrollLevels, 0x32);
        unsigned count = 0;
        for (StockListNode* n = list.head->next; n != list.head; n = n->next) ++count;
        if (count >= static_cast<unsigned>(spells.size())) break;
        if (++tries >= kScrollTries) break;
    }
    char* prototype = FindQuickItemNamed(TableObject(itemdb::QuickItems()), nullptr, game::SpellContainerText());
    for (StockListNode* n = list.head->next; n != list.head; n = n->next) {
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
EI_REPLACE_METHOD(0x0069AC20, ItemStock::GenerateScrolls);

// --- A-H25: the stock's lists filled by the class template (004A7080) ------------------------------

namespace {

// The stock's vectors are STLport vectors (push_back grows them as _M_insert_overflow does).
stlp::vector<char*>& AsVector(StockPtrVector& v) { return reinterpret_cast<stlp::vector<char*>&>(v); }
stlp::vector<StockMaterial>& AsVector(StockMaterialVector& v) {
    return reinterpret_cast<stlp::vector<StockMaterial>&>(v);
}
static_assert(sizeof(stlp::vector<char*>) == sizeof(StockPtrVector));
static_assert(sizeof(stlp::vector<StockMaterial>) == sizeof(StockMaterialVector));

void AddUnlessPresent(StockPtrVector& list, char* record) {
    if (!record) return;
    char** p = list.start;
    while (p != list.finish && *p != record) ++p;
    if (p == list.finish) AsVector(list).push_back(record);
}

} // namespace

void ItemStock::AddArmor(char* record) { AddUnlessPresent(armors, record); }
void ItemStock::AddWeapon(char* record) { AddUnlessPresent(weapons, record); }
void ItemStock::AddQuickItem(char* record) { AddUnlessPresent(quickItems, record); }
void ItemStock::AddSpell(char* record) { AddUnlessPresent(spells, record); }
void ItemStock::AddModifier(char* record) { AddUnlessPresent(modifiers, record); }
EI_REPLACE_METHOD(0x00699080, ItemStock::AddArmor);
EI_REPLACE_METHOD(0x006990E0, ItemStock::AddWeapon);
EI_REPLACE_METHOD(0x00699140, ItemStock::AddQuickItem);
EI_REPLACE_METHOD(0x006991A0, ItemStock::AddSpell);
EI_REPLACE_METHOD(0x00699200, ItemStock::AddModifier);

void ItemStock::AddMaterial(char* record, int minCount, int maxCount) {
    if (!record) return;
    StockMaterial* entry = materials.start;
    while (entry != materials.finish && entry->record != record) ++entry;
    if (entry == materials.finish) {
        AsVector(materials).push_back(StockMaterial{record, minCount, maxCount, maxCount});
        return;
    }
    entry->minCount = minCount;
    entry->maxCount = maxCount;
    entry->left = maxCount;
}
EI_REPLACE_METHOD(0x00699260, ItemStock::AddMaterial);
