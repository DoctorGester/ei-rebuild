// The class templates a new game builds and the item and spell lists each template gets from the databases.
// See class_templates.h and docs/notes/world.md ("Party records and class templates"). The templates live in
// a vector of counted pointers at +0x38 of the party manager object (its +0x24 holds the parties). The build
// makes five templates or, with `fourClasses`, four.
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

namespace {

// ---- Original code --------------------------------------------------------------------------------

// vector<counted pointer> members (stlp::CountedRefVector's): erase(first, last), _M_insert_overflow(pos, x,
// n), and the counted pointer's copy constructor (two identical instantiations, both RefPtr::CopyCounted).

// The item database (template +0x3C slot 18) lists the names of each kind into a CStringArray:
// weapons, armors, quick items, the fourth kind (its records
// have the class mask at +0x4C). The spell database (template +0xAC slot 22): spells,
// spell modifiers.
void ListWeapons(CItemManager* db, void*, console::CStringArray* names) { db->ListWeaponNames(names); }
void ListArmors(CItemManager* db, void*, console::CStringArray* names) { db->ListArmorNames(names); }
void ListQuickItems(CItemManager* db, void*, console::CStringArray* names) { db->ListQuickItemNames(names); }
void ListFourthKind(CItemManager* db, void*, console::CStringArray* names) { db->ListMaterialNames(names); }
void ListSpells(CSpellManager* db, void*, console::CStringArray* names) { db->ListSpellNames(names); }
void ListModifiers(CSpellManager* db, void*, console::CStringArray* names) { db->ListModifierNames(names); }
// SpellCodeByName / SpellModifierCodeByName (spell.cpp; __stdcall, the caller passes the database in ECX).

// Record lookups by name. Weapons / armors by char* (hash maps);
// operator[] of the string-keyed maps
// (a missing name is inserted with a null record); find_or_insert of
// the modifier map.
// (NamedRecordTable::Named and the tables' name maps.)

// The template's filter lists (+0x11C all, +0x1D4 spells and modifiers only): ItemStock::AddArmor and
// the like (__thiscall (record)) append the record to one vector unless already there; AddMaterial
// also takes two counts.

// The bit of the template's class (+0x08) in a record's class mask (SHL by CL: modulo 32).
bool Allowed(const ClassTemplate* t, std::uint32_t classMask) {
    std::uint32_t bit = 1u << ((static_cast<unsigned>(t->type) - 1) & 31);
    return (classMask & bit) != 0;
}

ItemStock* AllList(ClassTemplate* t) { return &t->allowed; }
ItemStock* MagicList(ClassTemplate* t) { return &t->magic; }

} // namespace

// ---- ClassTemplateFill ------------------------------------------------------------------------

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

// ---- Building the templates ------------------------------------------------------------------

namespace {

// Float fields +0x10..+0x30 (bit patterns as stored).
constexpr std::uint32_t kOne = 0x3F800000, kHalf = 0x3F000000, kFifth = 0x3E4CCCCD, kTenth = 0x3DCCCCCD;
constexpr std::uint32_t kDefaultWeights[9] = {kOne, kHalf, kFifth, kTenth, kOne, kHalf, kFifth, kTenth, kFifth};
constexpr std::uint32_t kZeroWeights[9] = {};

// One template: allocated (reference count word cleared first), constructed, its class, two flags
// and weights set, filled, then appended to the vector through a temporary counted pointer.
void AddTemplate(ClassTemplateVector* v, int type, std::uint8_t flag0C, std::uint8_t flag0D,
                 const std::uint32_t (&weights)[9]) {
    // The count dword is cleared before the allocation is tested; ours tests first.
    auto* t = new ClassTemplate();
    t->refs_ = 0;
    t->flags6_ = 0;
    t->flags7_ = 0;
    t->type = type;
    t->flag0C = flag0C;
    t->flag0D = flag0D;
    std::memcpy(t->weights, weights, sizeof(t->weights));
    ClassTemplateFill(t);

    ClassTemplate* temp = t;
    if (temp) ++temp->refs_;
    if (v->Finish() == v->EndOfStorage()) {
        stlp::AsRefPtrs(*v).InsertOverflow<stlp::CountedRefElement>(stlp::AsRefPtr(v->Finish()), stlp::AsRefPtr(temp), 1);
    } else {
        if (v->Finish()) reinterpret_cast<RefPtr*>(v->Finish())->CopyCounted(reinterpret_cast<const RefPtr*>(&temp));
        ++v->Finish();
    }
    if (temp && --temp->refs_ == 0) delete temp;
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
EI_REWRITTEN(0x004A74F0, TemplateTable::Find);

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
