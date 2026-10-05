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
#include <cstring>

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

// - GenerateWeaponInstructions / GenerateArmorInstructions / GenerateQuickItemInstructions
//   (00699810 / 006995E0 / 00699A40; ours only, they draw from rand and create items): the output
//   list gained one item per record of the stock's weapons / armors / quick items (quick items named
//   "scroll" skipped), in order, each counted, with +0x54 the record's index in its table and +0x48
//   within [minCount, maxCount] (maxCount when minCount >= maxCount); the scratch vector is empty.
//   Both fields are written for every item, so they are no leftover heap bytes.
namespace {

const StockPtrVector& TakenPrototypes() { return *reinterpret_cast<const StockPtrVector*>(0x007C8680); }

using GenerateInstructionsFn = void(__fastcall*)(ItemStock*, void*, int, int, StockList*);
GenerateInstructionsFn s_unusedWeaponInstructions;
GenerateInstructionsFn s_unusedArmorInstructions;
GenerateInstructionsFn s_unusedQuickInstructions;

bool IsScroll(const char* record) {
    const char* name = *reinterpret_cast<const char* const*>(record + 8);
    return std::strcmp(name, reinterpret_cast<const char*>(0x00796384)) == 0;   // "scroll"
}

constexpr std::size_t kMaxRecords = 512;

void CheckInstructions(const char* name, ItemStock* self, const StockPtrVector& source, std::uintptr_t table, int recordSize,
                       bool skipScrolls, int minCount, int maxCount, StockList* out,
                       void (ItemStock::*generate)(int, int, StockList*)) {
    std::size_t before = ListSize(out);
    // The expected record indices, in order (the function does not change the source vector).
    int indices[kMaxRecords];
    std::size_t expected = 0;
    for (char** p = source.start; p != source.finish; ++p) {
        if (skipScrolls && IsScroll(*p)) continue;
        std::intptr_t offset = reinterpret_cast<std::intptr_t>(*p) - *reinterpret_cast<const std::intptr_t*>(table);
        if (expected < kMaxRecords) indices[expected] = static_cast<int>(offset) / recordSize;
        ++expected;
    }
    (self->*generate)(minCount, maxCount, out);
    std::size_t added = ListSize(out) - before;
    int lo = minCount < maxCount ? minCount : maxCount;
    bool ok = added == expected && TakenPrototypes().start == TakenPrototypes().finish;
    std::size_t i = 0;
    for (const StockListNode* node = out->head->next; ok && node != out->head; node = node->next, ++i) {
        if (i < before) continue;
        const char* item = static_cast<const char*>(node->value);
        if (!item || *reinterpret_cast<const int*>(item + 4) < 1) {
            ok = false;
            break;
        }
        int count = *reinterpret_cast<const int*>(item + 0x48);
        if (count < lo || count > maxCount) ok = false;
        std::size_t k = i - before;
        if (k < kMaxRecords && *reinterpret_cast<const int*>(item + 0x54) != indices[k]) ok = false;
    }
    ei::verify::Check(name, true, ok, "%d..%d, %u records: %u items", minCount, maxCount, static_cast<unsigned>(expected),
                      static_cast<unsigned>(added));
}

} // namespace

static void __fastcall GenerateWeaponInstructionsVerify(ItemStock* self, void*, int minCount, int maxCount, StockList* out) {
    CheckInstructions("ItemStock::GenerateWeaponInstructions", self, self->weapons, 0x007C86F8, 0xB4, false, minCount, maxCount,
                      out, &ItemStock::GenerateWeaponInstructions);
}
EI_VERIFY(0x00699810, GenerateWeaponInstructionsVerify, 0, s_unusedWeaponInstructions);

static void __fastcall GenerateArmorInstructionsVerify(ItemStock* self, void*, int minCount, int maxCount, StockList* out) {
    CheckInstructions("ItemStock::GenerateArmorInstructions", self, self->armors, 0x007C8718, 200, false, minCount, maxCount, out,
                      &ItemStock::GenerateArmorInstructions);
}
EI_VERIFY(0x006995E0, GenerateArmorInstructionsVerify, 0, s_unusedArmorInstructions);

static void __fastcall GenerateQuickItemInstructionsVerify(ItemStock* self, void*, int minCount, int maxCount, StockList* out) {
    CheckInstructions("ItemStock::GenerateQuickItemInstructions", self, self->quickItems, 0x007C8738, 0x8C, true, minCount,
                      maxCount, out, &ItemStock::GenerateQuickItemInstructions);
}
EI_VERIFY(0x00699A40, GenerateQuickItemInstructionsVerify, 0, s_unusedQuickInstructions);
