// The trader stock generation (0069AE20, 0069A110, 00699810 / 006995E0 / 00699A40): see item_stock.h and docs/notes/items_spells.md
// ("Trader stock"). The STLport containers are the original's: their allocating members are called
// through the game, the pointer copies they inline are written out here.
#include "item_stock.h"

#include "item.h"
#include "random.h"

#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "../lib/stlp.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"

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
    char* EndCreateItem();                          // 006A5CF0
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
__declspec(naked) char* Constructor::EndCreateItem() EI_ORIGINAL(0x006A5CF0)
__declspec(naked) int Constructor::Cancel() EI_ORIGINAL(0x006A5D30)
__declspec(naked) bool Constructor::MagicCapacity(float*, float*) EI_ORIGINAL(0x006A6480)
static_assert(offsetof(Constructor, count) == offsetof(CItemConstructor, count_));

// 006A4230: an item's category (thiscall).
__declspec(naked) int __fastcall ItemCategory(char*) EI_ORIGINAL(0x006A4230)
// 0069D380: the class-0x15 prototype table's record by name (thiscall on 007C8778).
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
                                             char* const* /*value*/) EI_ORIGINAL(0x0069C3D0)
__declspec(naked) void __fastcall SpellListConstruct(StockList*, void*, const void* /*allocator*/) EI_ORIGINAL(0x0069C440)

// ---- Globals --------------------------------------------------------------------------------------

// Scratch vectors: the prototypes taken (007C8680), the candidate materials of one prototype
// (007C8650, pointers into the stock's material entries), the material entries taken (007C8640).
StockPtrVector& TakenPrototypes() { return *reinterpret_cast<StockPtrVector*>(0x007C8680); }
struct CandidateVector {
    StockMaterial** start;
    StockMaterial** finish;
    StockMaterial** eos;
};
CandidateVector& Candidates() { return *reinterpret_cast<CandidateVector*>(0x007C8650); }
StockMaterialVector& TakenMaterials() { return *reinterpret_cast<StockMaterialVector*>(0x007C8640); }
// Spell tables for weapons and armors (0069A480's argument).
void* const kWeaponSpellTable = reinterpret_cast<void*>(0x007C8568);
void* const kArmorSpellTable = reinterpret_cast<void*>(0x007C855C);
void* const kTable15 = reinterpret_cast<void*>(0x007C8778);

IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }
void*& NodeFreeList() { return *reinterpret_cast<void**>(0x0079B228); }
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

template <typename T>
T& At(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

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

// vector<char*>::operator=, as the original inlines it.
void AssignPointers(StockPtrVector& to, const StockPtrVector& from) {
    std::size_t length = from.size();
    if (length > static_cast<std::size_t>(to.eos - to.start)) {
        char** block = PtrAllocateCopy(&to, nullptr, length, from.start, from.finish);
        PtrDeallocate(to.start, static_cast<std::size_t>(to.eos - to.start));
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
void Output(StockList* out, char* item) {
    StockListNode* result;
    ListInsert(out, nullptr, &result, out->head, &item);
}

// The item's reference count (+0x04).
void AddReference(char* item) { ++At<int>(item, 4); }

// The record pointers of a prototype: +0x08 name, +0x10 material class, +0x34 price (float), +0x4C
// material units; virtual slot 0 the item kind.
const char* PrototypeName(char* p) { return At<const char*>(p, 0x08); }
const char* MaterialClass(char* p) { return At<const char*>(p, 0x10); }

// Picks the material of a prototype from the candidates (pointers into the stock's entries). Each
// candidate in turn is taken with a chance by its quality q (material +0x10) against the
// prototype's price p: 10% outside [2p/7 - p/5, p/5], else 110 - |(q - p/7) / (p/7 - p/5)| x 100
// percent (truncated). Null when none was taken.
StockMaterial* DrawMaterial(char* prototype) {
    float seventh = Round(At<float>(prototype, 0x34) * At<float>(reinterpret_cast<void*>(0x00743D78), 0));
    float fifth = Round(At<float>(prototype, 0x34) * At<float>(reinterpret_cast<void*>(0x0073BAD4), 0));
    CandidateVector& c = Candidates();
    if (c.start == c.finish) return nullptr;
    float low = Round((seventh + seventh) - fifth);
    for (StockMaterial** q = c.start; q != c.finish; ++q) {
        int chance = 10;
        float quality = At<float>((*q)->record, 0x10);
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

// Frees the spell list as the original's clear and destructor do: each counted spell released
// (slot 4), each node back to the node pool, then the sentinel.
void DestroySpellList(StockList& list) {
    StockListNode* head = list.head;
    StockListNode* node = head->next;
    while (node != head) {
        StockListNode* next = node->next;
        if (node->value) ei::VCall<void>(node->value, 4);
        node->next = static_cast<StockListNode*>(NodeFreeList());
        NodeFreeList() = node;
        node = next;
    }
    head->next = head;
    head->prev = head;
    // (The destructor's own clear finds the list empty.)
    stlp::deallocate(head, sizeof(StockListNode));
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
    void* spell = node->value;
    if (!(At<float>(spell, 0x40) > power)) ctor->SetSpellObject(spell);
}

} // namespace

// ---- 0069AE20 -------------------------------------------------------------------------------------

// Original bug kept: a failed build (EndCreateItem null) is dereferenced.
void ItemStock::GenerateItems(StockPtrVector* prototypes, int minCount, int maxCount, StockList* out, int kind) {
    Constructor* ctor = ei::VCall<Constructor*>(context, 16);
    void* manager = ei::VCall<void*>(context, 18);
    if (kind == 0 || kind == 3)
        CallFillSpellMaps(this, nullptr, kWeaponSpellTable);
    else if (kind == 1)
        CallFillSpellMaps(this, nullptr, kArmorSpellTable);

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
        if (Compare(PrototypeName(prototype), Text(0x0079391C)) == 0) continue;   // "wand"
        ctor->SetManager(manager);
        ctor->BeginCreateItem(ei::VCall<int>(prototype, 0), -1);
        ctor->SetPrototype(prototype);
        candidates.finish = candidates.start;   // erase(begin, end)
        if (CompareNoCase(MaterialClass(prototype), Text(0x007846F0)) != 0) {   // "none"
            for (StockMaterial* e = materials.start; e != materials.finish; ++e) {
                if (Compare(MaterialClass(prototype), At<const char*>(e->record, 4)) != 0) continue;
                if (At<int>(prototype, 0x4C) > e->left) continue;
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
            if (CompareNoCase(MaterialClass(prototype), Text(0x007846F0)) != 0) {
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
            chosen->left -= At<int>(prototype, 0x4C);
            ctor->SetMaterial(chosen->record);
        } else {
            ctor->SetMaterial(nullptr);
        }

        StockList spellList;
        char allocator = 0;
        SpellListConstruct(&spellList, nullptr, &allocator);
        if (kind == 1 || kind == 3 || kind == 0) AddSpell(this, ctor, spellList);
        char* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        AddReference(item);
        if (At<int>(item, 0x10) == 0x3006 && ItemCategory(item) == 8)
            At<int>(item, 0x48) = gamecrt::rand() * 30 / 0x7FFF % 30 + 20;
        Output(out, item);
        DestroySpellList(spellList);
    }
    candidates.finish = candidates.start;
}
EI_REPLACE_METHOD(0x0069AE20, ItemStock::GenerateItems);

// ---- 0069A110 -------------------------------------------------------------------------------------

void ItemStock::GenerateMaterials(int minCount, int maxCount, StockList* out) {
    Constructor* ctor = ei::VCall<Constructor*>(context, 16);
    void* manager = ei::VCall<void*>(context, 18);
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
    char* prototype = FindTable15Named(kTable15, nullptr, Text(0x007938C4));   // "material"
    for (StockMaterial* e = taken.start; e != taken.finish; ++e) {
        ctor->SetManager(manager);
        ctor->BeginCreateItem(0x3007, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(e->record);
        int range = e->maxCount - e->minCount + 1;
        ctor->count = gamecrt::rand() * range / 0x7FFF % range + e->minCount;
        char* item = ctor->EndCreateItem();
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
// whose vector starts at `table` (records of `recordSize` bytes) and +0x48 a random count.
void GenerateInstructions(ItemStock* stock, const StockPtrVector& source, std::uintptr_t prototypeName, std::uintptr_t table,
                          int recordSize, const char* skip, int minCount, int maxCount, StockList* out) {
    Constructor* ctor = ei::VCall<Constructor*>(stock->context, 16);
    void* manager = ei::VCall<void*>(stock->context, 18);
    StockPtrVector& taken = TakenPrototypes();
    taken.finish = taken.start;   // erase(begin, end)
    if (&source != &taken) AssignPointers(taken, source);
    char* prototype = FindTable15Named(kTable15, nullptr, Text(prototypeName));
    for (char** p = taken.start; p != taken.finish; ++p) {
        if (skip && Compare(PrototypeName(*p), skip) == 0) continue;
        ctor->SetManager(manager);
        ctor->BeginCreateItem(0x3007, -1);
        ctor->SetPrototype(prototype);
        ctor->SetMaterial(nullptr);
        char* item = ctor->EndCreateItem();
        ctor->SetManager(nullptr);
        std::intptr_t offset = reinterpret_cast<std::intptr_t>(*p) - *reinterpret_cast<const std::intptr_t*>(table);
        At<int>(item, 0x54) = static_cast<int>(offset) / recordSize;
        At<int>(item, 0x48) = RandomCount(minCount, maxCount);
        AddReference(item);
        Output(out, item);
    }
    taken.finish = taken.start;
}

} // namespace

void ItemStock::GenerateWeaponInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, weapons, 0x00796370, 0x007C86F8, 0xB4, nullptr, minCount, maxCount, out);   // "instruction weapon"
}
EI_REPLACE_METHOD(0x00699810, ItemStock::GenerateWeaponInstructions);

void ItemStock::GenerateArmorInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, armors, 0x0079635C, 0x007C8718, 200, nullptr, minCount, maxCount, out);   // "instruction armor"
}
EI_REPLACE_METHOD(0x006995E0, ItemStock::GenerateArmorInstructions);

// Quick items named "scroll" get no instruction.
void ItemStock::GenerateQuickItemInstructions(int minCount, int maxCount, StockList* out) {
    GenerateInstructions(this, quickItems, 0x0079638C, 0x007C8738, 0x8C, Text(0x00796384), minCount, maxCount,
                         out);   // "instruction quick item"
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
__declspec(naked) char* __fastcall RecipeMapInsert(void*, void*, const void* /*pair*/) EI_ORIGINAL(0x0069D130)
struct RecipeIterator {
    char* node;     // +0 next, +4 key (string), +0x10 the list (its sentinel)
    void* map;
};
__declspec(naked) void __fastcall RecipeMapBegin(void*, void*, RecipeIterator*) EI_ORIGINAL(0x0069C160)
__declspec(naked) char* __fastcall RecipeMapNextBucket(RecipeIterator*) EI_ORIGINAL(0x0069D0E0)
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
__declspec(naked) char* __fastcall ModifierMapAt(void*, void*, const void* /*pair*/) EI_ORIGINAL(0x0069D1E0)
// The spell records by name (map 007C851C, operator[] 004ABD00: inserts a null for a new name).
__declspec(naked) char** __fastcall SpellByName(void*, void*, const void* /*string*/) EI_ORIGINAL(0x004ABD00)
void* const kSpellsByName = reinterpret_cast<void*>(0x007C851C);
// The spell prototypes by name (thiscall on 007C8510, 00613920) and the spell manager's builder
// (00689310, RET 8: prototype, modifier list; a new counted spell or null).
__declspec(naked) char* __fastcall FindSpellPrototype(void*, void*, const char*) EI_ORIGINAL(0x00613920)
void* const kSpellPrototypes = reinterpret_cast<void*>(0x007C8510);
__declspec(naked) void* __fastcall BuildSpell(void*, void*, char* /*prototype*/, StockList* /*modifiers*/)
    EI_ORIGINAL(0x00689310)
// The modifier list (list<modifier*>): constructor 0069BF70 (an allocator argument), end 0068C500,
// insert 0068C560 (result, position, &value), node free 0068C640 (fastcall: node, count).
__declspec(naked) void __fastcall ModifierListConstruct(StockList*, void*, const void*) EI_ORIGINAL(0x0069BF70)
__declspec(naked) void __fastcall ModifierListEnd(StockList*, void*, StockListNode**) EI_ORIGINAL(0x0068C500)
__declspec(naked) void __fastcall ModifierListInsert(StockList*, void*, StockListNode**, StockListNode*, char* const*)
    EI_ORIGINAL(0x0068C560)
__declspec(naked) void __fastcall ModifierNodesFree(StockListNode*, int) EI_ORIGINAL(0x0068C640)
// The output list of counted spells: end 0069BE60, insert 0069C410 (result, position, &value).
__declspec(naked) void __fastcall SpellListEnd(StockList*, void*, StockListNode**) EI_ORIGINAL(0x0069BE60)
__declspec(naked) void __fastcall SpellListInsert(StockList*, void*, StockListNode**, StockListNode*, void* const*)
    EI_ORIGINAL(0x0069C410)

// The modifier map's lookup pair: a copied key and a null value (0x10 bytes).
struct ModifierKey {
    stlp::string key;
    void* value = nullptr;
    explicit ModifierKey(const stlp::string& s) : key(s) {}
};
static_assert(sizeof(ModifierKey) == 0x10);

// The modifier map's entry for `name` (created null when missing, as operator[] does).
char*& ModifierSlot(ItemStock* stock, const stlp::string& name) {
    ModifierKey pair(name);
    char* found = ModifierMapAt(stock->modifierMap, nullptr, &pair);
    return At<char*>(found, 0xC);
}

// A modifier record: +0x04 / +0x05 its kind letters, +0x10 the cost added, +0x14 the strength (a
// cost factor for kind 'm'), +0x18 the level it adds.
char ModifierKind(char* m) { return m[4]; }
float ModifierCost(char* m) { return At<float>(m, 0x10); }
float ModifierFactor(char* m) { return At<float>(m, 0x14); }
int ModifierLevel(char* m) { return At<int>(m, 0x18); }

struct StringVector {
    const stlp::string* start;
    const stlp::string* finish;
    const stlp::string* eos;
    unsigned size() const { return static_cast<unsigned>(finish - start); }
};

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
        if (!(At<std::uint32_t>(r, 0x30) & (1u << (classIndex & 31)))) continue;
        char* spell = *SpellByName(kSpellsByName, nullptr, r);
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
        char* inserted = RecipeMapInsert(spellMap, nullptr, pair);
        RecipePairDestroy(pair);
        RecipeListDestroy(&list);
        StockList* entries = reinterpret_cast<StockList*>(inserted + 0xC);
        StockListNode* end;
        RecipeListEnd(entries, nullptr, &end);
        StockListNode* result;
        RecipeListInsert(entries, nullptr, &result, end, &value);
    }

    for (char** p = modifiers.start; p != modifiers.finish; ++p) {
        char* m = *p;
        char letters[2] = {m[4], m[5]};
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
    void* spellManager = ei::VCall<void*>(context, 22);
    StockList chosen;
    char allocator = 0;
    ModifierListConstruct(&chosen, nullptr, &allocator);
    RecipeIterator it;
    RecipeMapBegin(spellMap, nullptr, &it);
    while (it.node) {
        StockListNode* head = At<StockListNode*>(it.node, 0x10);
        for (StockListNode* n = head->next; n != head; n = n->next) {
            if (GlobalRandom().Next() % 101 < threshold) continue;
            ClearModifierList(chosen);
            int count = 0;
            char* recipe = static_cast<char*>(n->value);
            char* prototype = FindSpellPrototype(kSpellPrototypes, nullptr, At<stlp::string>(recipe, 0).data());
            int level = At<int>(prototype, 0x44);
            int budget = x87::fistp(levels[At<int>(prototype, 0x14)]);
            float cost = At<float>(prototype, 0x18);
            const StringVector& required = At<StringVector>(recipe, 0x0C);
            for (unsigned i = 0; i < required.size(); ++i) {
                char* m = ModifierSlot(this, required.start[i]);
                if (!m) continue;
                AppendModifier(chosen, m);
                cost = Round(cost + ModifierCost(m));
                ++count;
                level += ModifierLevel(m);
            }
            if (budget < level || count > 8) continue;
            const StringVector& optional = At<StringVector>(recipe, 0x18);
            for (unsigned i = 0; i < optional.size() && count < 8; ++i) {
                if (GlobalRandom().Next() % 101 < 50) continue;
                char* m = ModifierSlot(this, optional.start[i]);
                if (!m || budget < ModifierLevel(m) + level) continue;
                AppendModifier(chosen, m);
                cost = Round(cost + ModifierCost(m));
                ++count;
                level += ModifierLevel(m);
            }
            char* repeated = ModifierSlot(this, At<stlp::string>(recipe, 0x24));
            if (repeated && budget >= ModifierLevel(repeated) + level) {
                int reached = level;   // (the original keeps it in a register; `level` is not updated)
                while (count < 8) {
                    AppendModifier(chosen, repeated);
                    ++count;
                    reached += ModifierLevel(repeated);
                    if (ModifierKind(repeated) == 'm') {
                        cost = Round(ModifierFactor(repeated) * At<float>(prototype, 0x18) + cost + ModifierCost(repeated));
                        if (!(cost > At<float>(prototype, 0x18))) break;
                    } else {
                        cost = Round(cost + ModifierCost(repeated));
                    }
                    if (budget < ModifierLevel(repeated) + reached) break;
                }
            }
            void* spell = BuildSpell(spellManager, nullptr, prototype, &chosen);
            if (spell) ++At<int>(spell, 4);
            StockListNode* end;
            SpellListEnd(out, nullptr, &end);
            StockListNode* result;
            SpellListInsert(out, nullptr, &result, end, &spell);
            if (spell) ei::VCall<void>(spell, 4);
        }
        char* next = At<char*>(it.node, 0);
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
__declspec(naked) char* __fastcall FindArmorNamed(void*, void*, const char*) EI_ORIGINAL(0x004AB820)
__declspec(naked) char* __fastcall FindWeaponNamed(void*, void*, const char*) EI_ORIGINAL(0x004AB720)
__declspec(naked) char* __fastcall FindQuickItemNamed(void*, void*, const char*) EI_ORIGINAL(0x00613820)
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
void ReadInt(void* stream, void* value) { ei::VCall<std::uint32_t>(stream, 1, value, 4u); }

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
void LoadRecords(void* stream, mfc::CString& name, unsigned& count, std::uintptr_t table, FindNamedFn find,
                 StockPtrVector& list) {
    ReadInt(stream, &count);
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        name.MakeLower();
        char* record = find(reinterpret_cast<void*>(table), nullptr, name);
        if (record) AddUnique(list, record, &PushItemRecord);
    }
}

// A map section: a count, then names (as read) looked up in an STLport map by string.
void LoadMapped(void* stream, mfc::CString& name, unsigned& count, std::uintptr_t map, MapAtFn at, StockPtrVector& list,
                PushFn push) {
    ReadInt(stream, &count);
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        char* record;
        {
            stlp::string key(static_cast<const char*>(name));
            record = *at(reinterpret_cast<void*>(map), nullptr, &key);
        }
        if (record) AddUnique(list, record, push);
    }
}

} // namespace

// Original behaviour kept: a material entry is pushed even when its material is not found (null
// record), and the two map lookups insert null entries for unknown names.
void ItemStock::LoadLists(void* stream) {
    ei::VCall<void*>(context, 22);   // (the spell and item managers, fetched and not used)
    ei::VCall<void*>(context, 18);
    mfc::CString name;
    unsigned count = 0;
    LoadRecords(stream, name, count, 0x007C8718, &FindArmorNamed, armors);
    LoadRecords(stream, name, count, 0x007C86F8, &FindWeaponNamed, weapons);
    LoadRecords(stream, name, count, 0x007C8738, &FindQuickItemNamed, quickItems);

    ReadInt(stream, &count);
    int values[3] = {0, 0, 0};
    for (unsigned i = 0; i < count; ++i) {
        ReadName(stream, name);
        name.MakeLower();
        StockMaterial entry;
        for (int& v : values) ReadInt(stream, &v);
        entry.record = FindMaterialNamed(reinterpret_cast<void*>(0x007C86D8), nullptr, name);
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

    LoadMapped(stream, name, count, 0x007C851C, &SpellByName, spells, &PushSpellRecord);
    LoadMapped(stream, name, count, 0x007C853C, &ModifierByName, modifiers, &PushModifierRecord);
}
EI_REPLACE_METHOD(0x0069B5B0, ItemStock::LoadLists);
