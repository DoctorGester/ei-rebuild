// A class template's trader stock (A-S4): the levels setter 004A6DC0 and the two generators the
// camp calls when its constructor record is due (004A6DF0 items, 004A6F60 magic). The generators
// collect new items in an STLport list<CItem*>, hand each to the template's party and release it.
// Names are provisional; see docs/notes/items_spells.md ("Trader stock").
#include "class_templates.h"

#include "item.h"
#include "party.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "game_data.h"

namespace {

// list<CItem*>'s constructor (004A9CD0 inlined): a sentinel node from the node pool linked to itself.
void ListConstruct(StockList* list) {
    auto* head = stlp::allocate_as<StockListNode>();
    list->head = head;
    head->next = head;
    head->prev = head;
}

// Each listed item goes to `party` (keeping its count, merged with an equal one) and is released;
// then the nodes and the sentinel go back to the pool (the clear and destructor inlined).
void HandOverAndFree(StockList* list, CParty* party) {
    StockListNode* head = list->head;
    for (StockListNode* node = head->next; node != head; node = node->next) {
        CItem* item = node->data;
        party->AddItem(item, 1, 1);
        item->Release();
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

// The vectors' constructor: no storage.
template <typename T>
void VectorConstruct(stlp::vector<T>* v) {
    v->Start() = nullptr;
    v->Finish() = nullptr;
    v->EndOfStorage() = nullptr;
}

} // namespace

ItemStock* ItemStock::Construct(CParty* owner) {
    context = owner;
    classIndex = 0;
    VectorConstruct(&armors);
    VectorConstruct(&weapons);
    VectorConstruct(&quickItems);
    VectorConstruct(&materials);
    VectorConstruct(&spells);
    VectorConstruct(&modifiers);
    // The maps' constructors as 004A66C0 inlines them (_M_initialize_buckets(100): 004A9FC0 /
    // 004AA270).
    spellMap.ConstructBuckets(100);
    modifierMap.ConstructBuckets(100);
    return this;
}
EI_REWRITTEN(0x004A66C0, ItemStock::Construct);

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

// 004A6580 is the constructor; the registration is Construct's.
ClassTemplate* ClassTemplate::Construct() { return new (this) ClassTemplate(); }
EI_REPLACE_METHOD(0x004A6580, ClassTemplate::Construct);

void ItemStock::Destruct() {
    context = nullptr;
    modifierMap.Destroy();
    spellMap.Destroy();
    // _Vector_base's destructors: the storage back to the node allocator (operator delete above 128 bytes).
    modifiers.FreeStorage();
    spells.FreeStorage();
    materials.FreeStorage();
    quickItems.FreeStorage();
    weapons.FreeStorage();
    armors.FreeStorage();
}

ClassTemplate::~ClassTemplate() {
    ItemsParty()->Destroy();
    SpellsParty()->Destroy();
    magic.Destruct();
    allowed.Destruct();
    SpellsParty()->~CParty();
    ItemsParty()->~CParty();
}
EI_REWRITTEN_SPECIAL(0x004A6A90, ClassTemplate::~ClassTemplate);
EI_REWRITTEN_SPECIAL(0x004A6A70, ClassTemplate::~ClassTemplate);

void ClassTemplate::SetLevels(const float* base, const float* scroll) {
    allowed.SetLevels(base, scroll);
    magic.SetLevels(base, scroll);
}
EI_REWRITTEN(0x004A6DC0, ClassTemplate::SetLevels);

void __fastcall ClassTemplateGenerateItems(ClassTemplate* t) {
    if (!t->flag0C) return;
    StockList list;
    ListConstruct(&list);
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
    HandOverAndFree(&list, t->ItemsParty());
}
EI_REWRITTEN(0x004A6DF0, ClassTemplateGenerateItems);

void __fastcall ClassTemplateGenerateMagic(ClassTemplate* t) {
    if (!t->flag0D) return;
    StockList list;
    ListConstruct(&list);
    t->SpellsParty()->Clear(1, 0);
    ItemStock& stock = t->magic;
    stock.classIndex = t->type - 1;
    stock.GenerateScrolls(40, 50, &list);
    stock.GenerateSpellInstructions(1, 3, &list);
    stock.GenerateModifierInstructions(20, 50, &list);
    HandOverAndFree(&list, t->SpellsParty());
}
EI_REWRITTEN(0x004A6F60, ClassTemplateGenerateMagic);
