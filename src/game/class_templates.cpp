// The class templates a new game builds (004A7630) and the item and spell lists each template
// gets from the databases (004A7080). See class_templates.h and docs/notes/world.md
// ("Party records and class templates").
//
// The templates live in a vector of counted pointers at +0x38 of the object at 007AA5A0 (its +0x24
// holds the parties). 004A88A0 builds five templates, 004A88C0 (from 0057F280) four.
#include "class_templates.h"

#include "console.h"
#include "core_refcount.h"
#include "item.h"
#include "item_db.h"
#include "item_stock.h"
#include "party.h"
#include "party_manager.h"
#include "lib/crt.h"

#include "ei/hook.h"
#include "lib/stlp_hash.h"

#include <cstring>
#include "originals_library.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "spell.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

// ---- Original code --------------------------------------------------------------------------------

// vector<counted pointer> members: 004A9A00 erase(first, last), 004AB520 _M_insert_overflow(pos,
// x, n), and the counted pointer's copy constructor (two identical instantiations, 004A5D50 and
// 004ABE30, both RefPtr::CopyCounted).
__declspec(naked) ClassTemplate** __fastcall VectorErase(ClassTemplateVector*, void*, ClassTemplate**, ClassTemplate**)
    EI_ORIGINAL(0x004A9A00)
__declspec(naked) void __fastcall VectorInsertOverflow(ClassTemplateVector*, void*, ClassTemplate**,
                                                       ClassTemplate* const*, unsigned) EI_ORIGINAL(0x004AB520)
using CopyConstructFn = void(__fastcall*)(ClassTemplate**, void*, ClassTemplate* const*);
void __fastcall CopyConstruct(ClassTemplate** a0, void*, ClassTemplate* const* a2) {
    reinterpret_cast<RefPtr*>(a0)->CopyCounted(reinterpret_cast<const RefPtr*>(a2));
}

// The item database (template +0x3C slot 18) lists the names of each kind into a CStringArray:
// 006AD2F0 weapons, 006AD380 armors, 006AD410 quick items, 006AD260 the fourth kind (its records
// have the class mask at +0x4C). The spell database (template +0xAC slot 22): 00689570 spells,
// 00689610 spell modifiers (originals_gameplay.h).
void ListWeapons(void* db, void*, console::CStringArray* names) { static_cast<CItemManager*>(db)->ListWeaponNames(names); }
void ListArmors(void* db, void*, console::CStringArray* names) { static_cast<CItemManager*>(db)->ListArmorNames(names); }
void ListQuickItems(void* db, void*, console::CStringArray* names) { static_cast<CItemManager*>(db)->ListQuickItemNames(names); }
void ListFourthKind(void* db, void*, console::CStringArray* names) { static_cast<CItemManager*>(db)->ListMaterialNames(names); }
void ListSpells(void* db, void*, console::CStringArray* names) { static_cast<CSpellManager*>(db)->ListSpellNames(names); }
void ListModifiers(void* db, void*, console::CStringArray* names) { static_cast<CSpellManager*>(db)->ListModifierNames(names); }
// 006893F0 / 006894B0 (SpellCodeByName / SpellModifierCodeByName, spell.cpp; __stdcall, the
// caller passes the database in ECX).

// Record lookups by name. 004AB720 / 004AB820: weapons / armors by char* (hash maps 007C86F8 /
// 007C8718); 004ABBD0 / 004ABAA0 / 004ABD00: operator[] of the string-keyed maps 007C8744 /
// 007C86E4 / 007C851C (a missing name is inserted with a null record); 004ACC10 find_or_insert of
// the modifier map 007C853C.
void* const kWeapons = reinterpret_cast<void*>(0x007C86F8);
void* const kArmors = reinterpret_cast<void*>(0x007C8718);
stlp::NameMap& QuickItemMap() { return *reinterpret_cast<stlp::NameMap*>(0x007C8744); }
stlp::NameMap& FourthKindMap() { return *reinterpret_cast<stlp::NameMap*>(0x007C86E4); }

// The template's filter lists (+0x11C all, +0x1D4 spells and modifiers only), __thiscall (record):
// each appends the record to one vector unless already there. 00699260 also takes two counts.
void AddArmor(void* list, void*, void* record) { static_cast<ItemStock*>(list)->AddArmor(static_cast<char*>(record)); }
void AddWeapon(void* list, void*, void* record) { static_cast<ItemStock*>(list)->AddWeapon(static_cast<char*>(record)); }
void AddQuickItem(void* list, void*, void* record) { static_cast<ItemStock*>(list)->AddQuickItem(static_cast<char*>(record)); }
void AddSpell(void* list, void*, void* record) { static_cast<ItemStock*>(list)->AddSpell(static_cast<char*>(record)); }
void AddModifier(void* list, void*, void* record) { static_cast<ItemStock*>(list)->AddModifier(static_cast<char*>(record)); }

// The bit of the template's class (+0x08) in a record's class mask (SHL by CL: modulo 32).
bool Allowed(const ClassTemplate* t, std::uint32_t classMask) {
    std::uint32_t bit = 1u << ((static_cast<unsigned>(t->type) - 1) & 31);
    return (classMask & bit) != 0;
}
bool Allowed(const ClassTemplate* t, const void* itemRecord) {
    return Allowed(t, static_cast<const itemdb::ItemRecordHead*>(itemRecord)->classMask);
}

void* AllList(ClassTemplate* t) { return &t->allowed; }
void* MagicList(ClassTemplate* t) { return &t->magic; }

} // namespace

// ---- 004A7080 -------------------------------------------------------------------------------------

void __fastcall ClassTemplateFill(ClassTemplate* t) {
    console::CStringArray names;
    void* items = t->ItemsParty()->ItemManager();
    void* spells = t->SpellsParty()->SpellManager();

    ListWeapons(items, nullptr, &names);
    for (int i = 0; i < names.size; ++i) {
        void* record = stlp::RecordNamed(static_cast<stlp::RecordsByName*>(kWeapons), nullptr, names.data[i]);
        if (record && Allowed(t, record)) AddWeapon(AllList(t), nullptr, record);
    }
    ListArmors(items, nullptr, &names);
    for (int i = 0; i < names.size; ++i) {
        void* record = stlp::RecordNamed(static_cast<stlp::RecordsByName*>(kArmors), nullptr, names.data[i]);
        if (record && Allowed(t, record)) AddArmor(AllList(t), nullptr, record);
    }
    ListQuickItems(items, nullptr, &names);
    for (int i = 0; i < names.size; ++i) {
        stlp::string key(names.data[i]);
        void* record = stlp::NamedSlot<void>(QuickItemMap(), key);
        if (record && Allowed(t, record)) AddQuickItem(AllList(t), nullptr, record);
    }
    ListFourthKind(items, nullptr, &names);
    for (int i = 0; i < names.size; ++i) {
        stlp::string key(names.data[i]);
        void* record = stlp::NamedSlot<void>(FourthKindMap(), key);
        if (record && Allowed(t, static_cast<itemdb::MaterialRecordHead*>(record)->classMask)) t->allowed.AddMaterial(static_cast<char*>(record), 0x14, 0x32);
    }

    // Spells and modifiers: the name's code is the key; allowed ones go to both lists.
    ListSpells(spells, nullptr, &names);
    for (int i = 0; i < names.size; ++i) {
        const char* code = SpellCodeByName(names.data[i]);
        stlp::string key(code);
        void* record = stlp::NamedSlot<void>(stock::SpellsByName(), key);
        if (record && Allowed(t, static_cast<itemdb::SpellRecord*>(record)->classMask)) {
            AddSpell(MagicList(t), nullptr, record);
            AddSpell(AllList(t), nullptr, record);
        }
    }
    ListModifiers(spells, nullptr, &names);
    for (int i = 0; i < names.size; ++i) {
        const char* code = SpellModifierCodeByName(names.data[i]);
        // operator[] inlined: the code as a string, a (key, null) pair built from it, find_or_insert.
        stlp::string key(code);
        auto* record = stlp::NamedSlot<itemdb::SpellModifierRecord>(stock::ModifiersByName(), key);
        if (record && Allowed(t, record->classMask)) {
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
    auto* memory = static_cast<ClassTemplate*>(gamecrt::operator_new(sizeof(ClassTemplate)));
    memory->refCount = 0;
    memory->refsHigh = 0;
    std::memset(memory->weightsTail, 0, sizeof(memory->weightsTail));   // SV7b2: never set, saved
    ClassTemplate* t = memory ? static_cast<ClassTemplate*>(memory)->Construct() : nullptr;
    t->type = type;
    t->flag0C = flag0C;
    t->flag0D = flag0D;
    std::memcpy(t->weights, weights, sizeof(t->weights));
    ClassTemplateFill(t);

    ClassTemplate* temp = t;
    if (temp) ++temp->refCount;
    if (v->Finish() == v->EndOfStorage()) {
        VectorInsertOverflow(v, nullptr, v->Finish(), &temp, 1);
    } else {
        if (v->Finish()) copyConstruct(v->Finish(), nullptr, &temp);
        ++v->Finish();
    }
    if (temp && --temp->refCount == 0 && temp) delete reinterpret_cast<CRefCounted*>(temp);   // slot 0 with 1
}

} // namespace

void __fastcall ClassTemplatesBuild(ClassTemplateVector* v, void*, unsigned fourClasses) {
    VectorErase(v, nullptr, v->begin(), v->end());
    if (static_cast<std::uint8_t>(fourClasses)) {
        AddTemplate(v, 1, 1, 1, kDefaultWeights, CopyConstruct);
        AddTemplate(v, 3, 1, 1, kDefaultWeights, CopyConstruct);
        AddTemplate(v, 4, 1, 1, kDefaultWeights, CopyConstruct);
        AddTemplate(v, 5, 1, 1, kDefaultWeights, CopyConstruct);
    } else {
        AddTemplate(v, 1, 1, 0, kDefaultWeights, CopyConstruct);
        AddTemplate(v, 2, 0, 1, kDefaultWeights, CopyConstruct);
        AddTemplate(v, 3, 1, 1, kDefaultWeights, CopyConstruct);
        AddTemplate(v, 4, 1, 1, kDefaultWeights, CopyConstruct);
        AddTemplate(v, 5, 1, 1, kZeroWeights, CopyConstruct);
    }
}
EI_REPLACE(0x004A7630, ClassTemplatesBuild);

// ---- Small members ----------------------------------------------------------------------------------

ClassTemplate* __fastcall ClassTemplatesFind(const ClassTemplateVector* v, void*, int type) {
    unsigned count = static_cast<unsigned>(v->size());
    for (unsigned i = 0; i < count; ++i)
        if ((*v)[i]->type == type) return (*v)[i];
    return nullptr;
}
EI_REPLACE(0x004A74F0, ClassTemplatesFind);

unsigned __fastcall ClassTemplatesQuestCompleted(void* manager) {
    ClassTemplateVector* v = &static_cast<PartyManager*>(manager)->templates;
    unsigned i = 0;
    while (i < static_cast<unsigned>(v->size())) {
        ++i;
        (*v)[i - 1]->flag38 = 1;
    }
    return i;
}
EI_REPLACE(0x004A88E0, ClassTemplatesQuestCompleted);
