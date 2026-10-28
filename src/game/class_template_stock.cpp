// A class template's trader stock: the levels setter and the two generators the
// camp calls when its constructor record is due (items, magic). The generators
// collect new items in an STLport list<CItem*>, hand each to the template's party and release it.
// Names are provisional; see docs/notes/items_spells.md ("Trader stock").
#include "class_templates.h"

#include "item.h"
#include "party.h"

#include "lib/crt.h"
#include "lib/stlp.h"
#include "game_data.h"

#include <iterator>
#include <memory>

namespace {

// Each listed item goes to `party` (keeping its count, merged with an equal one) and is released.
void HandOver(const StockList& list, CParty* party) {
    for (CItem* item : list) {
        party->AddItem(item, 1, 1);
        item->Release();
    }
}

} // namespace

ItemStock* ItemStock::Construct(CParty* owner) {
    context = owner;
    classIndex = 0;
    std::construct_at(&spellMap);
    std::construct_at(&modifierMap);
    return this;
}

ClassTemplate::ClassTemplate() : weightsTail{} {
    new (ItemsParty()) CParty();
    new (SpellsParty()) CParty();
    allowed.Construct(ItemsParty());
    magic.Construct(SpellsParty());
    ItemsParty()->Create();
    SpellsParty()->Create();
    type = -1;
    flag0C = 0;
    flag0D = 0;
    flag38 = 1;
}

void ItemStock::Destruct() {
    context = nullptr;
    std::destroy_at(&modifierMap);
    std::destroy_at(&spellMap);
}

ClassTemplate::~ClassTemplate() {
    ItemsParty()->Destroy();
    SpellsParty()->Destroy();
    magic.Destruct();
    allowed.Destruct();
    SpellsParty()->~CParty();
    ItemsParty()->~CParty();
}

void ClassTemplate::SetLevels(const float* base, const float* scroll) {
    allowed.SetLevels(base, scroll);
    magic.SetLevels(base, scroll);
}

void ClassTemplateGenerateItems(ClassTemplate* t) {
    if (!t->flag0C) return;
    StockList list;
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
    if (!t->flag0D) return;
    StockList list;
    t->SpellsParty()->Clear(1, 0);
    ItemStock& stock = t->magic;
    stock.classIndex = t->type - 1;
    stock.GenerateScrolls(40, 50, &list);
    stock.GenerateSpellInstructions(1, 3, &list);
    stock.GenerateModifierInstructions(20, 50, &list);
    HandOver(list, t->SpellsParty());
}
