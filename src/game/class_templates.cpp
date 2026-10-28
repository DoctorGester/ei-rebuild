// The class templates a new game builds and the item and spell lists each template gets from the databases.
// See docs/notes/world.md ("Party records and class templates"). The build makes five templates or, with
// `fourClasses`, four.
#include "class_templates.h"

#include "console.h"
#include "core_refcount.h"
#include "item.h"
#include "item_db.h"
#include "item_stock.h"
#include "party.h"
#include "party_manager.h"
#include "spell.h"

#include "lib/crt.h"

#include <cstring>

namespace {

// The template's class bit in a record's class mask (the shift is modulo 32).
bool Allowed(const ClassTemplate* t, std::uint32_t classMask) {
    std::uint32_t bit = 1u << ((static_cast<unsigned>(t->type) - 1) & 31);
    return (classMask & bit) != 0;
}

ItemStock* AllList(ClassTemplate* t) { return &t->allowed; }
ItemStock* MagicList(ClassTemplate* t) { return &t->magic; }

// The weights (bit patterns as stored).
constexpr std::uint32_t kOne = 0x3F800000, kHalf = 0x3F000000, kFifth = 0x3E4CCCCD, kTenth = 0x3DCCCCCD;
constexpr std::uint32_t kDefaultWeights[9] = {kOne, kHalf, kFifth, kTenth, kOne, kHalf, kFifth, kTenth, kFifth};
constexpr std::uint32_t kZeroWeights[9] = {};

// One template, appended to the vector through a temporary counted pointer.
void AddTemplate(TemplateTable* v, int type, std::uint8_t sellsItems, std::uint8_t sellsSpells,
                 const std::uint32_t (&weights)[9]) {
    auto* t = new ClassTemplate();
    t->refs_ = 0;
    t->flags6_ = 0;
    t->flags7_ = 0;
    t->type = type;
    t->sellsItems = sellsItems;
    t->sellsSpells = sellsSpells;
    std::memcpy(t->weights, weights, sizeof(t->weights));
    ClassTemplateFill(t);

    v->emplace_back(t);
}

} // namespace

void ClassTemplateFill(ClassTemplate* t) {
    std::vector<std::string> names;
    CItemManager* items = t->ItemsParty()->ItemFactory();
    CSpellManager* spells = t->SpellsParty()->SpellManager();

    itemdb::ItemDatabase& db = *g_itemDatabase;
    items->ListWeaponNames(&names);
    for (const std::string& name : names) {
        itemdb::WeaponRecord* record = db.weapons.Named(name);
        if (record && Allowed(t, record->classMask)) AllList(t)->AddWeapon(record);
    }
    items->ListArmorNames(&names);
    for (const std::string& name : names) {
        itemdb::ArmorRecord* record = db.armors.Named(name);
        if (record && Allowed(t, record->classMask)) AllList(t)->AddArmor(record);
    }
    items->ListQuickItemNames(&names);
    for (const std::string& key : names) {
        itemdb::QuickItemRecord* record = db.quickItems.names[key];
        if (record && Allowed(t, record->classMask)) AllList(t)->AddQuickItem(record);
    }
    items->ListMaterialNames(&names);
    for (const std::string& key : names) {
        itemdb::MaterialRecordHead* record = db.materials.names[key];
        if (record && Allowed(t, record->classMask)) t->allowed.AddMaterial(record, 0x14, 0x32);
    }

    // Spells and modifiers: the name's code is the key; allowed ones go to both lists.
    spells->ListSpellNames(&names);
    for (const std::string& name : names) {
        const char* code = SpellCodeByName(name);
        std::string key(code);
        itemdb::SpellRecord* record = stock::SpellsByName()[key];
        if (record && Allowed(t, record->classMask)) {
            MagicList(t)->AddSpell(record);
            AllList(t)->AddSpell(record);
        }
    }
    spells->ListModifierNames(&names);
    for (const std::string& name : names) {
        const char* code = SpellModifierCodeByName(name);
        std::string key(code);
        auto* record = stock::ModifiersByName()[key];
        if (record && Allowed(t, record->classMask)) {
            MagicList(t)->AddModifier(record);
            AllList(t)->AddModifier(record);
        }
    }
}

// ---- Building the templates ------------------------------------------------------------------

void TemplateTable::Build(unsigned fourClasses) {
    TemplateTable* v = this;
    v->clear();
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

// ---- Small members ----------------------------------------------------------------------------------

ClassTemplate* TemplateTable::Find(int type) const {
    unsigned count = static_cast<unsigned>(size());
    for (unsigned i = 0; i < count; ++i)
        if ((*this)[i]->type == type) return (*this)[i];
    return nullptr;
}

unsigned ClassTemplatesQuestCompleted(PartyManager* manager) {
    TemplateTable* v = &manager->templates;
    unsigned i = 0;
    while (i < static_cast<unsigned>(v->size())) {
        ++i;
        (*v)[i - 1]->restockDue = 1;
    }
    return i;
}
