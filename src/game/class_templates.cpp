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
#include "spell.h"
#include "lib/stlp_vector_refs.h"
#include "game_data.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

// ---- Original code --------------------------------------------------------------------------------

// vector<counted pointer> members (stlp::CountedRefVector's): 004A9A00 erase(first, last), 004AB520
// _M_insert_overflow(pos, x, n), and the counted pointer's copy constructor (two identical
// instantiations, 004A5D50 and 004ABE30, both RefPtr::CopyCounted).

// The item database (template +0x3C slot 18) lists the names of each kind into a CStringArray:
// 006AD2F0 weapons, 006AD380 armors, 006AD410 quick items, 006AD260 the fourth kind (its records
// have the class mask at +0x4C). The spell database (template +0xAC slot 22): 00689570 spells,
// 00689610 spell modifiers (originals_gameplay.h).
void ListWeapons(CItemManager* db, void*, console::CStringArray* names) { db->ListWeaponNames(names); }
void ListArmors(CItemManager* db, void*, console::CStringArray* names) { db->ListArmorNames(names); }
void ListQuickItems(CItemManager* db, void*, console::CStringArray* names) { db->ListQuickItemNames(names); }
void ListFourthKind(CItemManager* db, void*, console::CStringArray* names) { db->ListMaterialNames(names); }
void ListSpells(CSpellManager* db, void*, console::CStringArray* names) { db->ListSpellNames(names); }
void ListModifiers(CSpellManager* db, void*, console::CStringArray* names) { db->ListModifierNames(names); }
// 006893F0 / 006894B0 (SpellCodeByName / SpellModifierCodeByName, spell.cpp; __stdcall, the
// caller passes the database in ECX).

// Record lookups by name. 004AB720 / 004AB820: weapons / armors by char* (hash maps 007C86F8 /
// 007C8718); 004ABBD0 / 004ABAA0 / 004ABD00: operator[] of the string-keyed maps 007C8744 /
// 007C86E4 / 007C851C (a missing name is inserted with a null record); 004ACC10 find_or_insert of
// the modifier map 007C853C.
// (NamedRecordTable::Named and the tables' name maps.)

// The template's filter lists (+0x11C all, +0x1D4 spells and modifiers only): ItemStock::AddArmor and
// the like (__thiscall (record)) append the record to one vector unless already there; AddMaterial
// (00699260) also takes two counts.

// The bit of the template's class (+0x08) in a record's class mask (SHL by CL: modulo 32).
bool Allowed(const ClassTemplate* t, std::uint32_t classMask) {
    std::uint32_t bit = 1u << ((static_cast<unsigned>(t->type) - 1) & 31);
    return (classMask & bit) != 0;
}

ItemStock* AllList(ClassTemplate* t) { return &t->allowed; }
ItemStock* MagicList(ClassTemplate* t) { return &t->magic; }

} // namespace

// ---- 004A7080 -------------------------------------------------------------------------------------

void __fastcall ClassTemplateFill(ClassTemplate* t) {
    console::CStringArray names;
    CItemManager* items = t->ItemsParty()->ItemManager();
    CSpellManager* spells = t->SpellsParty()->SpellManager();

    itemdb::ItemDatabase& db = itemdb::TheItemDatabase();
    ListWeapons(items, nullptr, &names);
    for (int i = 0; i < names.size; ++i) {
        itemdb::WeaponRecord* record = db.weapons.Named(names.data[i]);
        if (record && Allowed(t, record->classMask)) AllList(t)->AddWeapon(record);
    }
    ListArmors(items, nullptr, &names);
    for (int i = 0; i < names.size; ++i) {
        itemdb::ArmorRecord* record = db.armors.Named(names.data[i]);
        if (record && Allowed(t, record->classMask)) AllList(t)->AddArmor(record);
    }
    ListQuickItems(items, nullptr, &names);
    for (int i = 0; i < names.size; ++i) {
        stlp::string key(names.data[i]);
        itemdb::QuickItemRecord* record = stlp::NamedSlot<itemdb::QuickItemRecord>(db.quickItems.names, key);
        if (record && Allowed(t, record->classMask)) AllList(t)->AddQuickItem(record);
    }
    ListFourthKind(items, nullptr, &names);
    for (int i = 0; i < names.size; ++i) {
        stlp::string key(names.data[i]);
        itemdb::MaterialRecordHead* record = stlp::NamedSlot<itemdb::MaterialRecordHead>(db.materials.names, key);
        if (record && Allowed(t, record->classMask)) t->allowed.AddMaterial(record, 0x14, 0x32);
    }

    // Spells and modifiers: the name's code is the key; allowed ones go to both lists.
    ListSpells(spells, nullptr, &names);
    for (int i = 0; i < names.size; ++i) {
        const char* code = SpellCodeByName(names.data[i]);
        stlp::string key(code);
        itemdb::SpellRecord* record = stlp::NamedSlot<itemdb::SpellRecord>(stock::SpellsByName(), key);
        if (record && Allowed(t, record->classMask)) {
            MagicList(t)->AddSpell(record);
            AllList(t)->AddSpell(record);
        }
    }
    ListModifiers(spells, nullptr, &names);
    for (int i = 0; i < names.size; ++i) {
        const char* code = SpellModifierCodeByName(names.data[i]);
        // operator[] inlined: the code as a string, a (key, null) pair built from it, find_or_insert.
        stlp::string key(code);
        auto* record = stlp::NamedSlot<itemdb::SpellModifierRecord>(stock::ModifiersByName(), key);
        if (record && Allowed(t, record->classMask)) {
            MagicList(t)->AddModifier(record);
            AllList(t)->AddModifier(record);
        }
    }
}
EI_REWRITTEN(0x004A7080, ClassTemplateFill);

// ---- 004A7630 -------------------------------------------------------------------------------------

namespace {

// Float fields +0x10..+0x30 (bit patterns as the original stores them).
constexpr std::uint32_t kOne = 0x3F800000, kHalf = 0x3F000000, kFifth = 0x3E4CCCCD, kTenth = 0x3DCCCCCD;
constexpr std::uint32_t kDefaultWeights[9] = {kOne, kHalf, kFifth, kTenth, kOne, kHalf, kFifth, kTenth, kFifth};
constexpr std::uint32_t kZeroWeights[9] = {};

// One template: allocated (reference count word cleared first), constructed, its class, two flags
// and weights set, filled, then appended to the vector through a temporary counted pointer.
void AddTemplate(ClassTemplateVector* v, int type, std::uint8_t flag0C, std::uint8_t flag0D,
                 const std::uint32_t (&weights)[9]) {
    auto* memory = gamecrt::operator_new_as<ClassTemplate>();
    memory->refCount = 0;
    memory->refsHigh = 0;
    std::memset(memory->weightsTail, 0, sizeof(memory->weightsTail));   // SV7b2: never set, saved
    ClassTemplate* t = memory ? memory->Construct() : nullptr;
    t->type = type;
    t->flag0C = flag0C;
    t->flag0D = flag0D;
    std::memcpy(t->weights, weights, sizeof(t->weights));
    ClassTemplateFill(t);

    ClassTemplate* temp = t;
    if (temp) ++temp->refCount;
    if (v->Finish() == v->EndOfStorage()) {
        stlp::AsRefPtrs(*v).InsertOverflow<stlp::CountedRefElement>(stlp::AsRefPtr(v->Finish()), stlp::AsRefPtr(temp), 1);
    } else {
        if (v->Finish()) reinterpret_cast<RefPtr*>(v->Finish())->CopyCounted(reinterpret_cast<const RefPtr*>(&temp));
        ++v->Finish();
    }
    if (temp && --temp->refCount == 0 && temp) delete reinterpret_cast<CRefCounted*>(temp);   // slot 0 with 1
}

} // namespace

void TemplateTable::Build(unsigned fourClasses) {
    TemplateTable* v = this;
    stlp::CountedRefVector::Clear(&stlp::AsRefPtrs(*v));
    if (static_cast<std::uint8_t>(fourClasses)) {
        AddTemplate(v, 1, 1, 1, kDefaultWeights);
        AddTemplate(v, 3, 1, 1, kDefaultWeights);
        AddTemplate(v, 4, 1, 1, kDefaultWeights);
        AddTemplate(v, 5, 1, 1, kDefaultWeights);
    } else {
        AddTemplate(v, 1, 1, 0, kDefaultWeights);
        AddTemplate(v, 2, 0, 1, kDefaultWeights);
        AddTemplate(v, 3, 1, 1, kDefaultWeights);
        AddTemplate(v, 4, 1, 1, kDefaultWeights);
        AddTemplate(v, 5, 1, 1, kZeroWeights);
    }
}
EI_REWRITTEN(0x004A7630, TemplateTable::Build);

// ---- Small members ----------------------------------------------------------------------------------

ClassTemplate* TemplateTable::Find(int type) const {
    unsigned count = static_cast<unsigned>(size());
    for (unsigned i = 0; i < count; ++i)
        if ((*this)[i]->type == type) return (*this)[i];
    return nullptr;
}
EI_REPLACE_METHOD(0x004A74F0, TemplateTable::Find);

unsigned __fastcall ClassTemplatesQuestCompleted(PartyManager* manager) {
    ClassTemplateVector* v = &manager->templates;
    unsigned i = 0;
    while (i < static_cast<unsigned>(v->size())) {
        ++i;
        (*v)[i - 1]->flag38 = 1;
    }
    return i;
}
EI_REWRITTEN(0x004A88E0, ClassTemplatesQuestCompleted);
