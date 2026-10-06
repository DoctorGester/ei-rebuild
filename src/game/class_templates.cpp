// The class templates a new game builds (004A7630) and the item and spell lists each template
// gets from the databases (004A7080). See class_templates.h and docs/notes/world.md
// ("Party records and class templates").
//
// The templates live in a vector of counted pointers at +0x38 of the object at 007AA5A0 (its +0x24
// holds the parties). 004A88A0 builds five templates, 004A88C0 (from 0057F280) four.
#include "class_templates.h"

#include "console.h"
#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstring>
#include "originals_library.h"
#include "originals_engine.h"
#include "originals_gameplay.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

// ---- Original code --------------------------------------------------------------------------------

// The template: 004A6580 constructor (returns this), 004A7080 fill (through its address).
__declspec(naked) ClassTemplate* __fastcall TemplateConstruct(void*) EI_ORIGINAL(0x004A6580)
__declspec(naked) void __fastcall TemplateFillOrig(ClassTemplate*) EI_ORIGINAL(0x004A7080)

// vector<counted pointer> members: 004A9A00 erase(first, last), 004AB520 _M_insert_overflow(pos,
// x, n), and the counted pointer's copy constructor (two identical instantiations).
__declspec(naked) ClassTemplate** __fastcall VectorErase(ClassTemplateVector*, void*, ClassTemplate**, ClassTemplate**)
    EI_ORIGINAL(0x004A9A00)
__declspec(naked) void __fastcall VectorInsertOverflow(ClassTemplateVector*, void*, ClassTemplate**,
                                                       ClassTemplate* const*, unsigned) EI_ORIGINAL(0x004AB520)
using CopyConstructFn = void(__fastcall*)(ClassTemplate**, void*, ClassTemplate* const*);
__declspec(naked) void __fastcall CopyConstructA(ClassTemplate**, void*, ClassTemplate* const*) EI_ORIGINAL(0x004ABE30)
void __fastcall CopyConstructB(ClassTemplate** a0, void*, ClassTemplate* const* a2) { CountedPtrCopyOriginal((void*)a0, nullptr, (void* const*)a2); }

// The item database (template +0x3C slot 18) lists the names of each kind into a CStringArray:
// 006AD2F0 weapons, 006AD380 armors, 006AD410 quick items, 006AD260 the fourth kind (its records
// have the class mask at +0x4C). The spell database (template +0xAC slot 22): 00689570 spells,
// 00689610 spell modifiers. All __thiscall (CStringArray*).
using NameListFn = void(__fastcall*)(void* db, void*, console::CStringArray*);
__declspec(naked) void __fastcall ListWeapons(void*, void*, console::CStringArray*) EI_ORIGINAL(0x006AD2F0)
__declspec(naked) void __fastcall ListArmors(void*, void*, console::CStringArray*) EI_ORIGINAL(0x006AD380)
__declspec(naked) void __fastcall ListQuickItems(void*, void*, console::CStringArray*) EI_ORIGINAL(0x006AD410)
__declspec(naked) void __fastcall ListFourthKind(void*, void*, console::CStringArray*) EI_ORIGINAL(0x006AD260)
__declspec(naked) void __fastcall ListSpells(void*, void*, console::CStringArray*) EI_ORIGINAL(0x00689570)
__declspec(naked) void __fastcall ListModifiers(void*, void*, console::CStringArray*) EI_ORIGINAL(0x00689610)
// 006893F0 / 006894B0 (rewritten in spell.cpp, __stdcall; the caller passes the database in ECX).
__declspec(naked) char* __stdcall SpellCode(const char*) EI_ORIGINAL(0x006893F0)
__declspec(naked) char* __stdcall ModifierCode(const char*) EI_ORIGINAL(0x006894B0)

// Record lookups by name. 004AB720 / 004AB820: weapons / armors by char* (hash maps 007C86F8 /
// 007C8718); 004ABBD0 / 004ABAA0 / 004ABD00: operator[] of the string-keyed maps 007C8744 /
// 007C86E4 / 007C851C (a missing name is inserted with a null record); 004ACC10 find_or_insert of
// the modifier map 007C853C.
void* __fastcall FindWeapon(void* a0, void*, const char* a2) { return FindWeaponOriginal(a0, nullptr, a2); }
void* __fastcall FindArmor(void* a0, void*, const char* a2) { return FindArmorOriginal(a0, nullptr, a2); }
void** __fastcall QuickItemAt(void* a0, void*, const void* a2) { return QuickItemMapAtOriginal(a0, nullptr, a2); }
__declspec(naked) void** __fastcall FourthKindAt(void*, void*, const void*) EI_ORIGINAL(0x004ABAA0)
void** __fastcall SpellAt(void* a0, void*, const void* a2) { return SpellMapAtOriginal(a0, nullptr, a2); }
char* __fastcall ModifierFindOrInsert(void* a0, void*, const void* a2) { return (char*)ModifierMapFindOrInsertOriginal(a0, nullptr, a2); }
void* const kWeapons = reinterpret_cast<void*>(0x007C86F8);
void* const kArmors = reinterpret_cast<void*>(0x007C8718);
void* const kQuickItemMap = reinterpret_cast<void*>(0x007C8744);
void* const kFourthKindMap = reinterpret_cast<void*>(0x007C86E4);
void* const kSpellMap = reinterpret_cast<void*>(0x007C851C);
void* const kModifierMap = reinterpret_cast<void*>(0x007C853C);

// STLport strings (start, finish, end of storage), handled through the original members:
// 0040C5A0 string(const char*, allocator), 004217E0 string(allocator), 00428F10 assign(first,
// last), 004A8930 copy constructor, 0040C720 / 00425040 destructors (two instantiations).
struct RawString {
    char* start;
    char* finish;
    char* eos;
};
static RawString* StringFromChars(RawString* s, void*, const char* text, const void* alloc) { return static_cast<RawString*>(::StringInitFromChars(s, nullptr, text, alloc)); }
void __fastcall StringCopy(RawString* a0, void*, const RawString* a2) { StringCopyConstructOriginal((void*)a0, nullptr, (const void*)a2); }

// The template's filter lists (+0x11C all, +0x1D4 spells and modifiers only), __thiscall (record):
// each appends the record to one vector unless already there. 00699260 also takes two counts.
void AddArmor(void* list, void*, void* record) { StockAddArmorOriginal(list, nullptr, record); }
void AddWeapon(void* list, void*, void* record) { StockAddWeaponOriginal(list, nullptr, record); }
void AddQuickItem(void* list, void*, void* record) { StockAddQuickItemOriginal(list, nullptr, record); }
__declspec(naked) void __fastcall AddFourthKind(void*, void*, void*, int, int) EI_ORIGINAL(0x00699260)
void AddSpell(void* list, void*, void* record) { StockAddSpellOriginal(list, nullptr, record); }
void AddModifier(void* list, void*, void* record) { StockAddModifierOriginal(list, nullptr, record); }

template <typename T>
T& At(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

// The bit of the template's class (+0x08) in a record's class mask (SHL by CL: modulo 32).
bool Allowed(const ClassTemplate* t, const void* record, std::size_t maskOffset) {
    std::uint32_t bit = 1u << ((static_cast<unsigned>(t->type) - 1) & 31);
    return (At<std::uint32_t>(const_cast<void*>(record), maskOffset) & bit) != 0;
}

void* AllList(ClassTemplate* t) { return reinterpret_cast<char*>(t) + 0x11C; }
void* MagicList(ClassTemplate* t) { return reinterpret_cast<char*>(t) + 0x1D4; }

} // namespace

// ---- 004A7080 -------------------------------------------------------------------------------------

void __fastcall ClassTemplateFill(ClassTemplate* t) {
    console::CStringArray names;
    void* items = ei::VCall<void*>(reinterpret_cast<char*>(t) + 0x3C, 18);
    void* spells = ei::VCall<void*>(reinterpret_cast<char*>(t) + 0xAC, 22);
    char alloc = 0;   // the allocator argument (an empty object passed by address)

    ListWeapons(items, nullptr, &names);
    for (int i = 0; i < names.size_; ++i) {
        void* record = FindWeapon(kWeapons, nullptr, names.data_[i]);
        if (record && Allowed(t, record, 0x64)) AddWeapon(AllList(t), nullptr, record);
    }
    ListArmors(items, nullptr, &names);
    for (int i = 0; i < names.size_; ++i) {
        void* record = FindArmor(kArmors, nullptr, names.data_[i]);
        if (record && Allowed(t, record, 0x64)) AddArmor(AllList(t), nullptr, record);
    }
    ListQuickItems(items, nullptr, &names);
    for (int i = 0; i < names.size_; ++i) {
        RawString key;
        StringFromChars(&key, nullptr, names.data_[i], &alloc);
        void* record = *QuickItemAt(kQuickItemMap, nullptr, &key);
        StlStringDestroy(&key);
        if (record && Allowed(t, record, 0x64)) AddQuickItem(AllList(t), nullptr, record);
    }
    ListFourthKind(items, nullptr, &names);
    for (int i = 0; i < names.size_; ++i) {
        RawString key;
        StringFromChars(&key, nullptr, names.data_[i], &alloc);
        void* record = *FourthKindAt(kFourthKindMap, nullptr, &key);
        StlStringDestroy(&key);
        if (record && Allowed(t, record, 0x4C)) AddFourthKind(AllList(t), nullptr, record, 0x14, 0x32);
    }

    // Spells and modifiers: the name's code is the key; allowed ones go to both lists.
    ListSpells(spells, nullptr, &names);
    for (int i = 0; i < names.size_; ++i) {
        const char* code = SpellCode(names.data_[i]);
        RawString key;
        StringInitWithAllocator(&key, nullptr, &alloc);
        StringAssignSpan(&key, nullptr, code, code + std::strlen(code));
        void* record = *SpellAt(kSpellMap, nullptr, &key);
        StlStringDestroy(&key);
        if (record && Allowed(t, record, 0x80)) {
            AddSpell(MagicList(t), nullptr, record);
            AddSpell(AllList(t), nullptr, record);
        }
    }
    ListModifiers(spells, nullptr, &names);
    for (int i = 0; i < names.size_; ++i) {
        const char* code = ModifierCode(names.data_[i]);
        // operator[] inlined: an empty string assigned the code, a (key, null) pair built from it,
        // find_or_insert, the pair and the string destroyed.
        RawString key{};
        StringAssignSpan(&key, nullptr, code, code + std::strlen(code));
        struct {
            RawString name;
            void* record;
        } pair;
        StringCopy(&pair.name, nullptr, &key);
        pair.record = nullptr;
        char* element = ModifierFindOrInsert(kModifierMap, nullptr, &pair);
        StlStringDestroy(&pair.name);
        void* record = At<void*>(element, 0xC);
        StringBufferFree(&key);
        if (record && Allowed(t, record, 0x20)) {
            AddModifier(MagicList(t), nullptr, record);
            AddModifier(AllList(t), nullptr, record);
        }
    }
}
EI_REPLACE(0x004A7080, ClassTemplateFill);

// ---- 004A7630 -------------------------------------------------------------------------------------

namespace {

// Float fields +0x10..+0x30 (bit patterns as the original stores them).
constexpr std::uint32_t kOne = 0x3F800000, kHalf = 0x3F000000, kFifth = 0x3E4CCCCD, kTenth = 0x3DCCCCCD;
constexpr std::uint32_t kDefaultWeights[9] = {kOne, kHalf, kFifth, kTenth, kOne, kHalf, kFifth, kTenth, kFifth};
constexpr std::uint32_t kZeroWeights[9] = {};

// One template: allocated (reference count word cleared first), constructed, its class, two flags
// and weights set, filled, then appended to the vector through a temporary counted pointer.
void AddTemplate(ClassTemplateVector* v, int type, std::uint8_t flag0C, std::uint8_t flag0D,
                 const std::uint32_t (&weights)[9], CopyConstructFn copyConstruct) {
    void* memory = gamecrt::operator_new(0x28C);
    At<std::uint32_t>(memory, 4) = 0;
    ClassTemplate* t = memory ? TemplateConstruct(memory) : nullptr;
    t->type = type;
    t->flag0C = flag0C;
    t->flag0D = flag0D;
    std::memcpy(t->weights, weights, sizeof(t->weights));
    TemplateFillOrig(t);

    ClassTemplate* temp = t;
    if (temp) ++temp->refCount;
    if (v->finish == v->eos) {
        VectorInsertOverflow(v, nullptr, v->finish, &temp, 1);
    } else {
        if (v->finish) copyConstruct(v->finish, nullptr, &temp);
        ++v->finish;
    }
    if (temp && --temp->refCount == 0 && temp) ei::VCall<void>(temp, 0, 1);
}

} // namespace

void __fastcall ClassTemplatesBuild(ClassTemplateVector* v, void*, unsigned fourClasses) {
    VectorErase(v, nullptr, v->start, v->finish);
    if (static_cast<std::uint8_t>(fourClasses)) {
        AddTemplate(v, 1, 1, 1, kDefaultWeights, CopyConstructA);
        AddTemplate(v, 3, 1, 1, kDefaultWeights, CopyConstructA);
        AddTemplate(v, 4, 1, 1, kDefaultWeights, CopyConstructA);
        AddTemplate(v, 5, 1, 1, kDefaultWeights, CopyConstructA);
    } else {
        AddTemplate(v, 1, 1, 0, kDefaultWeights, CopyConstructA);
        AddTemplate(v, 2, 0, 1, kDefaultWeights, CopyConstructA);
        AddTemplate(v, 3, 1, 1, kDefaultWeights, CopyConstructA);
        AddTemplate(v, 4, 1, 1, kDefaultWeights, CopyConstructB);
        AddTemplate(v, 5, 1, 1, kZeroWeights, CopyConstructB);
    }
}
EI_REPLACE(0x004A7630, ClassTemplatesBuild);

// ---- Small members ----------------------------------------------------------------------------------

ClassTemplate* __fastcall ClassTemplatesFind(const ClassTemplateVector* v, void*, int type) {
    unsigned count = static_cast<unsigned>(v->finish - v->start);
    for (unsigned i = 0; i < count; ++i)
        if (v->start[i]->type == type) return v->start[i];
    return nullptr;
}
EI_REPLACE(0x004A74F0, ClassTemplatesFind);

unsigned __fastcall ClassTemplatesQuestCompleted(void* manager) {
    auto* v = reinterpret_cast<ClassTemplateVector*>(static_cast<char*>(manager) + 0x38);
    unsigned i = 0;
    while (i < static_cast<unsigned>(v->finish - v->start)) {
        ++i;
        reinterpret_cast<std::uint8_t*>(v->start[i - 1])[0x38] = 1;
    }
    return i;
}
EI_REPLACE(0x004A88E0, ClassTemplatesQuestCompleted);
