// Checks for the class template constructor (class_template_stock.cpp): ours only (the parties and
// maps allocate). Reached by every new game (004A7630 builds the templates) and by loading a save
// with templates (the archive's factory 00457850; world_load). ItemStock::Construct (004A66C0) has
// no other caller than the template's constructor, which calls it directly: covered here.
#include "verify.h"

#include "../../src/game/class_templates.h"
#include "../../src/game/party.h"

#include <cstdint>

namespace {

void* s_unused;

template <typename V>
bool Empty(const V& v) {
    return v.start == nullptr && v.finish == nullptr && v.eos == nullptr;
}

// A stock's map (raw bytes): an empty hash table with NextPrime(100) buckets, all null.
bool EmptyMap(const std::uint8_t* map) {
    auto* view = reinterpret_cast<const stlp::hash_map<stlp::string, void*, stlp::StringHash>*>(map);
    return view->Size() == 0 && view->BucketCount() == stlp::NextPrime(100) && view->First() == nullptr;
}

bool FreshStock(const ItemStock& stock, const void* context) {
    return stock.context == context && stock.classIndex == 0 && Empty(stock.armors) && Empty(stock.weapons) &&
           Empty(stock.quickItems) && Empty(stock.materials) && Empty(stock.spells) && Empty(stock.modifiers) &&
           EmptyMap(stock.spellMap) && EmptyMap(stock.modifierMap);
}

ClassTemplate* __fastcall ConstructVerify(ClassTemplate* self) {
    self->Construct();
    auto* items = reinterpret_cast<CParty*>(self->items);
    auto* spells = reinterpret_cast<CParty*>(self->spells);
    bool ok = self->vtable == reinterpret_cast<void*>(ClassTemplate::kVtableAddress) && self->type == -1 &&
              self->flag0C == 0 && self->flag0D == 0 && self->flag38 == 1 && items->itemManager_ != nullptr &&
              spells->itemManager_ != nullptr && FreshStock(self->allowed, items) && FreshStock(self->magic, spells);
    ei::verify::Check("ClassTemplate::Construct", true, ok, "template %p", self);
    return self;
}

} // namespace

EI_VERIFY(0x004A6580, ConstructVerify, 0, s_unused);
