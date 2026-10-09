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
        auto* item = static_cast<CItem*>(node->data);
        party->AddItem(item, 1, 1);
        static_cast<CEngineObject*>(node->data)->Release();
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

// The stock's two maps (0x14 bytes each, raw in ItemStock) as an STLport hash_map: only the bucket
// array and the count are set up here, so the element types do not matter.
using StockMapView = stlp::hash_map<stlp::string, void*, stlp::StringHash>;
static_assert(sizeof(StockMapView) == sizeof(ItemStock::spellMap));

// A map's constructor as 004A66C0 inlines it: the bucket vector and the count proxy cleared, then
// _M_initialize_buckets(100) (004A9FC0 / 004AA270).
void MapConstruct(std::uint8_t* map) { reinterpret_cast<StockMapView*>(map)->InitBuckets(100); }

// The maps' destructors (library; each clears the nodes and frees the bucket array): 004A9010 the
// modifier map, 004A8E60 the spell map.
__declspec(naked) void __fastcall ModifierMapDestroy(std::uint8_t* /*map*/) EI_ORIGINAL(0x004A9010)
__declspec(naked) void __fastcall SpellMapDestroy(std::uint8_t* /*map*/) EI_ORIGINAL(0x004A8E60)

// _Vector_base's destructor: the storage back to the node allocator (operator delete above 128
// bytes).
template <typename V>
void VectorFree(V* v) {
    if (v->start) stlp::deallocate(v->start, static_cast<std::size_t>(v->eos - v->start) * sizeof(*v->start));
}

template <typename V>
void VectorConstruct(V* v) {
    v->start = nullptr;
    v->finish = nullptr;
    v->eos = nullptr;
}

} // namespace

ItemStock* ItemStock::Construct(void* owner) {
    context = owner;
    classIndex = 0;
    VectorConstruct(&armors);
    VectorConstruct(&weapons);
    VectorConstruct(&quickItems);
    VectorConstruct(&materials);
    VectorConstruct(&spells);
    VectorConstruct(&modifiers);
    MapConstruct(spellMap);
    MapConstruct(modifierMap);
    return this;
}
EI_REPLACE_METHOD(0x004A66C0, ItemStock::Construct);

ClassTemplate* ClassTemplate::Construct() {
    vtable = reinterpret_cast<void*>(kBaseVtableAddress);
    ItemsParty(this)->Construct();
    SpellsParty(this)->Construct();
    allowed.Construct(ItemsParty(this));
    magic.Construct(SpellsParty(this));
    vtable = reinterpret_cast<void*>(kVtableAddress);
    ItemsParty(this)->Create();
    SpellsParty(this)->Create();
    type = -1;
    flag0C = 0;
    flag0D = 0;
    flag38 = 1;
    return this;
}
EI_REPLACE_METHOD(0x004A6580, ClassTemplate::Construct);

void ItemStock::Destruct() {
    context = nullptr;
    ModifierMapDestroy(modifierMap);
    SpellMapDestroy(spellMap);
    VectorFree(&modifiers);
    VectorFree(&spells);
    VectorFree(&materials);
    VectorFree(&quickItems);
    VectorFree(&weapons);
    VectorFree(&armors);
}

void ClassTemplate::Destruct() {
    vtable = reinterpret_cast<void*>(kVtableAddress);
    ItemsParty(this)->Destroy();
    SpellsParty(this)->Destroy();
    magic.Destruct();
    allowed.Destruct();
    SpellsParty(this)->DestructBody();
    ItemsParty(this)->DestructBody();
    vtable = reinterpret_cast<void*>(kBaseVtableAddress);
}
EI_REPLACE_METHOD(0x004A6A90, ClassTemplate::Destruct);

ClassTemplate* ClassTemplate::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004A6A70, ClassTemplate::DeletingDestruct);

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
