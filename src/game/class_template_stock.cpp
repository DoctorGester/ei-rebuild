// A class template's trader stock: the levels setter and the two generators the
// camp calls when its constructor record is due (items, magic). The generators
// collect new items in a vector, hand each to the template's party and release it.
// Names are provisional; see docs/notes/items_spells.md ("Trader stock").
#include "class_templates.h"

#include "item.h"
#include "party.h"

#include "lib/crt.h"

#include <vector>

namespace {

// Each listed item goes to `party` (keeping its count, merged with an equal one) and is released.
void HandOver(const std::vector<Item*>& list, CParty* party) {
    for (Item* item : list) {
        party->AddItem(item, 1, 1);
        item->Release();
    }
}

} // namespace

ClassTemplate::ClassTemplate() {
    items.Create();
    spells.Create();
}

ClassTemplate::~ClassTemplate() {
    items.Destroy();
    spells.Destroy();
}

void ClassTemplate::SetLevels(const float* base, const float* scroll) {
    allowed.SetLevels(base, scroll);
    magic.SetLevels(base, scroll);
}

void ClassTemplateGenerateItems(ClassTemplate* t) {
    if (!t->sellsItems) return;
    std::vector<Item*> list;
    t->ItemsParty()->Clear(1, 0);
    ItemStock& stock = t->allowed;
    stock.classIndex = t->type - 1;
    stock.CopyMagic(&t->magic);
    stock.ResetMaterialUnits();
    stock.GenerateWeapons(40, 50, &list);
    stock.GenerateArmors(40, 50, &list);
    stock.GenerateQuickItems(40, 50, &list);
    stock.GenerateWeaponInstructions(1, 3, &list);
    stock.GenerateArmorInstructions(1, 3, &list);
    stock.GenerateQuickItemInstructions(1, 3, &list);
    stock.GenerateMaterials(20, 50, &list);
    HandOver(list, t->ItemsParty());
}

void ClassTemplateGenerateMagic(ClassTemplate* t) {
    if (!t->sellsSpells) return;
    std::vector<Item*> list;
    t->SpellsParty()->Clear(1, 0);
    ItemStock& stock = t->magic;
    stock.classIndex = t->type - 1;
    stock.GenerateScrolls(&list);
    stock.GenerateSpellInstructions(1, 3, &list);
    stock.GenerateModifierInstructions(20, 50, &list);
    HandOver(list, t->SpellsParty());
}
