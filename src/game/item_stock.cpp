// The trader stock generation (0069AE20, 0069A110): see item_stock.h and docs/notes/items_spells.md
// ("Trader stock"). The STLport containers are the original's: their allocating members are called
// through the game, the pointer copies they inline are written out here.
#include "item_stock.h"

#include "item.h"
#include "random.h"

#include "../lib/crt.h"
#include "../lib/stlp.h"
#include "ei/hook.h"
#include "ei/vcall.h"

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
__declspec(naked) void __fastcall FillSpellMaps(ItemStock*, void*, void* /*table*/) EI_ORIGINAL(0x0069A480)
__declspec(naked) void __fastcall GenerateSpells(ItemStock*, void*, StockList* /*out*/, const float* /*levels*/, unsigned /*threshold*/)
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
    GenerateSpells(stock, nullptr, &spells, levels, 0);
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
        FillSpellMaps(this, nullptr, kWeaponSpellTable);
    else if (kind == 1)
        FillSpellMaps(this, nullptr, kArmorSpellTable);

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
