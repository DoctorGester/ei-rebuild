// The class templates a new game builds and the item and spell lists each template gets from the databases.
// See docs/notes/world.md ("Party records and class templates"). The build makes five templates or, with
// `fourClasses`, four.
#include "game/players/class_templates.h"

#include "game/console/console.h"
#include "engine/core/core_refcount.h"
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "game/spells/spell_db.h"
#include "game/items/item_manager.h"
#include "game/items/item_stock.h"
#include "game/players/party.h"
#include "game/players/party_manager.h"
#include "game/spells/spell.h"

#include "lib/crt.h"

#include <cstring>
#include <vector>

namespace {

// The template's class bit in a record's class mask (the shift is modulo 32).
bool Allowed(const ClassTemplate* t, std::uint32_t classMask) {
    std::uint32_t bit = 1u << ((static_cast<unsigned>(t->type) - 1) & 31);
    return (classMask & bit) != 0;
}

ItemStock* AllList(ClassTemplate* t) { return &t->allowed; }
ItemStock* MagicList(ClassTemplate* t) { return &t->magic; }

constexpr float kDefaultWeights[9] = {1.0f, 0.5f, 0.2f, 0.1f, 1.0f, 0.5f, 0.2f, 0.1f, 0.2f};
constexpr float kZeroWeights[9] = {};

// One template, appended to the vector through a temporary counted pointer.
void AddTemplate(std::vector<HeldRef<ClassTemplate>>* v, int type, std::uint8_t sellsItems, std::uint8_t sellsSpells,
                 const float (&weights)[9]) {
    auto* t = new ClassTemplate();
    t->type = type;
    t->sellsItems = sellsItems;
    t->sellsSpells = sellsSpells;
    std::memcpy(t->weights, weights, sizeof(t->weights));
    ClassTemplateFill(t);

    v->emplace_back(t);
}

// Each listed item goes to `party` (keeping its count, merged with an equal one) and is released.
void HandOver(const std::vector<Item*>& list, Party* party) {
    for (Item* item : list) {
        party->AddItem(item, true, true);
        item->Release();
    }
}

} // namespace

void ClassTemplateFill(ClassTemplate* t) {
    std::vector<std::string> names;
    ItemManager* items = t->ItemsParty()->ItemFactory();
    SpellManager* spells = t->SpellsParty()->GetSpellManager();

    itemdb::ItemDatabase& db = g_itemDatabase;
    names = items->ListWeaponNames();
    for (const std::string& name : names) {
        itemdb::WeaponRecord* record = db.weapons.Named(name);
        if (record && Allowed(t, record->classMask)) AllList(t)->AddWeapon(record);
    }
    names = items->ListArmorNames();
    for (const std::string& name : names) {
        itemdb::ArmorRecord* record = db.armors.Named(name);
        if (record && Allowed(t, record->classMask)) AllList(t)->AddArmor(record);
    }
    names = items->ListQuickItemNames();
    for (const std::string& key : names) {
        itemdb::QuickItemRecord* record = db.quickItems.names[key];
        if (record && Allowed(t, record->classMask)) AllList(t)->AddQuickItem(record);
    }
    names = items->ListMaterialNames();
    for (const std::string& key : names) {
        itemdb::MaterialRecordHead* record = db.materials.names[key];
        if (record && Allowed(t, record->classMask)) t->allowed.AddMaterial(record, 0x14, 0x32);
    }

    // Spells and modifiers: the name's code is the key; allowed ones go to both lists.
    names = spells->ListSpellNames();
    for (const std::string& name : names) {
        const std::string* code = SpellCodeByName(name);
        std::string key(*code);
        itemdb::SpellRecord* record = g_spellDatabase.spells.names[key];
        if (record && Allowed(t, record->classMask)) {
            MagicList(t)->AddSpell(record);
            AllList(t)->AddSpell(record);
        }
    }
    names = spells->ListModifierNames();
    for (const std::string& name : names) {
        const char* code = SpellModifierCodeByName(name);
        std::string key(code);
        auto* record = g_spellDatabase.modifiers.names[key];
        if (record && Allowed(t, record->classMask)) {
            MagicList(t)->AddModifier(record);
            AllList(t)->AddModifier(record);
        }
    }
}

// ---- Building the templates ------------------------------------------------------------------

void TemplateTable::Build(unsigned fourClasses) {
    std::vector<HeldRef<ClassTemplate>>* v = &templates;
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
    for (const HeldRef<ClassTemplate>& t : templates)
        if (t->type == type) return t;
    return nullptr;
}

unsigned ClassTemplatesQuestCompleted(PartyManager* manager) {
    const std::vector<HeldRef<ClassTemplate>>& all = manager->templates.All();
    for (const HeldRef<ClassTemplate>& t : all) t->restockDue = 1;
    return static_cast<unsigned>(all.size());
}

// --- Trader stock and lifetime -------------------------------------------------------------------

// A class template's trader stock: the levels setter and the two generators the camp calls when its constructor
// record is due (items, magic). The generators collect new items in a vector, hand each to the template's party and
// release it. Names are provisional; see docs/notes/items_spells.md ("Trader stock").
ClassTemplate::ClassTemplate() {
    items.Create();
    spells.Create();
}

ClassTemplate::~ClassTemplate() {
    items.Destroy();
    spells.Destroy();
}

void ClassTemplate::SetLevels(const float (&base)[kSchoolCount], const float (&scroll)[kSchoolCount]) {
    allowed.SetLevels(base, scroll);
    magic.SetLevels(base, scroll);
}

void ClassTemplateGenerateItems(ClassTemplate* t) {
    if (!t->sellsItems) return;
    std::vector<Item*> list;
    t->ItemsParty()->Clear(true, false);
    ItemStock& stock = t->allowed;
    stock.classIndex = t->type - 1;
    stock.CopyMagic(t->magic);
    stock.ResetMaterialUnits();
    stock.GenerateWeapons(40, 50, list);
    stock.GenerateArmors(40, 50, list);
    stock.GenerateQuickItems(40, 50, list);
    stock.GenerateWeaponInstructions(1, 3, list);
    stock.GenerateArmorInstructions(1, 3, list);
    stock.GenerateQuickItemInstructions(1, 3, list);
    stock.GenerateMaterials(20, 50, list);
    HandOver(list, t->ItemsParty());
}

void ClassTemplateGenerateMagic(ClassTemplate* t) {
    if (!t->sellsSpells) return;
    std::vector<Item*> list;
    t->SpellsParty()->Clear(true, false);
    ItemStock& stock = t->magic;
    stock.classIndex = t->type - 1;
    stock.GenerateScrolls(list);
    stock.GenerateSpellInstructions(1, 3, list);
    stock.GenerateModifierInstructions(20, 50, list);
    HandOver(list, t->SpellsParty());
}
