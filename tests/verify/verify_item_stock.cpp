// Checks for the trader stock generation (src/game/item_stock.cpp). Both functions draw from the CRT
// and ISAAC generators and create items, so only ours runs (scenario ui_camp: the camp's first
// opening after a new game builds the stock of the template, whose +0x38 flag its constructor set).
//
// - GenerateItems (0069AE20): the output list gained at most one item per prototype taken (the
//   random count, or all prototypes when there are not more), each counted at least once and of the
//   weapon / armor / quick item kinds (0x3004..0x3006); the candidate scratch vector is empty; no
//   material entry's units went below zero.
// - GenerateMaterials (0069A110): the output list gained exactly as many items as entries taken (the
//   count is within the clipped range), each counted; the scratch vector is empty.
#include "verify.h"

#include "../../src/game/item_stock.h"

#include <climits>

namespace {

std::size_t ListSize(const StockList* list) {
    std::size_t n = 0;
    for (const StockListNode* node = list->head->next; node != list->head; node = node->next) ++n;
    return n;
}

// The items after the first `skip` entries all non-null and counted, with kinds in [lo, hi].
bool NewItemsValid(const StockList* list, std::size_t skip, int lo, int hi) {
    std::size_t i = 0;
    for (const StockListNode* node = list->head->next; node != list->head; node = node->next, ++i) {
        if (i < skip) continue;
        const char* item = static_cast<const char*>(node->value);
        if (!item || *reinterpret_cast<const int*>(item + 4) < 1) return false;
        int kind = *reinterpret_cast<const int*>(item + 0x10);
        if (kind < lo || kind > hi) return false;
    }
    return true;
}

bool NoNegativeUnits(const ItemStock* stock, const int* before, std::size_t count) {
    for (std::size_t i = 0; i < count && i < stock->materials.size(); ++i)
        if (stock->materials.start[i].left < 0 && before[i] >= 0) return false;
    return true;
}

const StockPtrVector& Candidates() { return *reinterpret_cast<const StockPtrVector*>(0x007C8650); }
const StockMaterialVector& TakenMaterials() { return *reinterpret_cast<const StockMaterialVector*>(0x007C8640); }

using GenerateItemsFn = void(__fastcall*)(ItemStock*, void*, StockPtrVector*, int, int, StockList*, int);
GenerateItemsFn s_unusedItems;
using GenerateMaterialsFn = void(__fastcall*)(ItemStock*, void*, int, int, StockList*);
GenerateMaterialsFn s_unusedMaterials;

} // namespace

static void __fastcall GenerateItemsVerify(ItemStock* self, void*, StockPtrVector* prototypes, int minCount, int maxCount,
                                           StockList* out, int kind) {
    std::size_t before = ListSize(out);
    std::size_t prototypeCount = prototypes->size();
    int units[256];
    std::size_t materialCount = self->materials.size();
    for (std::size_t i = 0; i < materialCount && i < 256; ++i) units[i] = self->materials.start[i].left;
    self->ItemStock::GenerateItems(prototypes, minCount, maxCount, out, kind);
    std::size_t after = ListSize(out);
    // The count drawn never exceeds maxCount.
    std::size_t limit = prototypeCount;
    std::size_t upper = static_cast<std::size_t>(maxCount > 0 ? maxCount : 0);
    if (upper < limit) limit = upper;
    bool ok = after >= before && after - before <= limit && NewItemsValid(out, before, 0x3004, 0x3006) &&
              Candidates().start == Candidates().finish && NoNegativeUnits(self, units, materialCount < 256 ? materialCount : 256);
    ei::verify::Check("ItemStock::GenerateItems", true, ok, "kind %d, %d..%d of %u prototypes: %u -> %u items", kind, minCount,
                      maxCount, static_cast<unsigned>(prototypeCount), static_cast<unsigned>(before), static_cast<unsigned>(after));
}
EI_VERIFY(0x0069AE20, GenerateItemsVerify, 0, s_unusedItems);

static void __fastcall GenerateMaterialsVerify(ItemStock* self, void*, int minCount, int maxCount, StockList* out) {
    std::size_t before = ListSize(out);
    std::size_t size = self->materials.size();
    self->ItemStock::GenerateMaterials(minCount, maxCount, out);
    std::size_t added = ListSize(out) - before;
    int lowCount = minCount < maxCount ? minCount : maxCount;
    std::size_t lo = lowCount > 0 ? static_cast<std::size_t>(lowCount) : 0;
    std::size_t hi = maxCount > 0 ? static_cast<std::size_t>(maxCount) : 0;
    if (lo > size) lo = size;
    if (hi > size) hi = size;
    bool ok = added >= lo && added <= hi && NewItemsValid(out, before, INT_MIN, INT_MAX) &&
              TakenMaterials().start == TakenMaterials().finish;
    ei::verify::Check("ItemStock::GenerateMaterials", true, ok, "%d..%d of %u entries: %u items", minCount, maxCount,
                      static_cast<unsigned>(size), static_cast<unsigned>(added));
}
EI_VERIFY(0x0069A110, GenerateMaterialsVerify, 0, s_unusedMaterials);
