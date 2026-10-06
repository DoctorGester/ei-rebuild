// A class template's trader stock (A-S4): the levels setter 004A6DC0 and the two generators the
// camp calls when its constructor record is due (004A6DF0 items, 004A6F60 magic). The generators
// collect new items in an STLport list<CItem*>, hand each to the template's party and release it.
// Names are provisional; see docs/notes/items_spells.md ("Trader stock").
#include "class_templates.h"

#include "item.h"
#include "party.h"

#include "ei/hook.h"
#include "lib/stlp.h"

namespace {

// The template's two party objects (0x70 bytes each, built by the template's constructor).
CParty* ItemsParty(ClassTemplate* t) { return reinterpret_cast<CParty*>(t->items); }
CParty* SpellsParty(ClassTemplate* t) { return reinterpret_cast<CParty*>(t->spells); }

// list<CItem*>'s constructor (004A9CD0 inlined): a sentinel node from the node pool linked to itself.
void ListConstruct(StockList* list) {
    auto* head = static_cast<StockListNode*>(stlp::allocate(sizeof(StockListNode)));
    list->head = head;
    head->next = head;
    head->prev = head;
}

// Each listed item goes to `party` (keeping its count, merged with an equal one) and is released;
// then the nodes and the sentinel go back to the pool (the clear and destructor inlined).
void HandOverAndFree(StockList* list, CParty* party) {
    StockListNode* head = list->head;
    for (StockListNode* node = head->next; node != head; node = node->next) {
        auto* item = static_cast<CItem*>(node->value);
        party->AddItem(item, 1, 1);
        static_cast<CEngineObject*>(node->value)->Release();
    }
    for (StockListNode* node = head->next; node != head;) {
        StockListNode* next = node->next;
        stlp::deallocate(node, sizeof(StockListNode));
        node = next;
    }
    head->next = head;
    head->prev = head;
    stlp::deallocate(head, sizeof(StockListNode));
}

} // namespace

void __fastcall ClassTemplateSetLevels(ClassTemplate* t, void*, const float* base, const float* scroll) {
    t->allowed.SetLevels(base, scroll);
    t->magic.SetLevels(base, scroll);
}
EI_REPLACE(0x004A6DC0, ClassTemplateSetLevels);

void __fastcall ClassTemplateGenerateItems(ClassTemplate* t) {
    if (!t->flag0C) return;
    StockList list;
    ListConstruct(&list);
    ItemsParty(t)->Clear(1, 0);
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
    HandOverAndFree(&list, ItemsParty(t));
}
EI_REPLACE(0x004A6DF0, ClassTemplateGenerateItems);

void __fastcall ClassTemplateGenerateMagic(ClassTemplate* t) {
    if (!t->flag0D) return;
    StockList list;
    ListConstruct(&list);
    SpellsParty(t)->Clear(1, 0);
    ItemStock& stock = t->magic;
    stock.classIndex = t->type - 1;
    stock.GenerateScrolls(40, 50, &list);
    stock.GenerateSpellInstructions(1, 3, &list);
    stock.GenerateModifierInstructions(20, 50, &list);
    HandOverAndFree(&list, SpellsParty(t));
}
EI_REPLACE(0x004A6F60, ClassTemplateGenerateMagic);
