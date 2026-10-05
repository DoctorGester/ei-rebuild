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
#include "../../src/lib/stlp.h"
#include "ei/hook.h"

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

// - FillSpellMaps (0069A480; ours only: it empties and refills the stock's own maps, so the
//   original cannot run on the same object): the recipe map holds exactly the recipes for the
//   stock's class whose spell is offered; every modifier key holds the modifier a replay of the
//   rules picks (strongest per letter for a/d/e/m/r/t, the last per letter pair for f/i). The
//   lookups only find keys ours already inserted.
// - GenerateSpells (0069A780; ours only, it draws from ISAAC and builds spells): the output list
//   gained at most one spell per recipe in the map, each null or counted.
// Both are reached from GenerateItems (kinds 0 / 1 / 3) in scenario trader_stock.
namespace {

__declspec(naked) char** __fastcall SpellByNameForCheck(void*, void*, const void*) EI_ORIGINAL(0x004ABD00)
__declspec(naked) char* __fastcall ModifierMapAtForCheck(void*, void*, const void*) EI_ORIGINAL(0x0069D1E0)

using FillSpellMapsFn = void(__fastcall*)(ItemStock*, void*, const SpellRecipeVector*);
FillSpellMapsFn s_unusedFillSpellMaps;
using GenerateSpellsFn = void(__fastcall*)(ItemStock*, void*, StockList*, const float*, unsigned);
GenerateSpellsFn s_unusedGenerateSpells;

// The recipes in the stock's recipe hash_map (+0x50: buckets at +4 / +8; node +0 next, +0x10 the
// list sentinel).
std::size_t RecipeCount(ItemStock* self) {
    char** buckets = *reinterpret_cast<char***>(self->spellMap + 4);
    char** end = *reinterpret_cast<char***>(self->spellMap + 8);
    std::size_t n = 0;
    for (char** b = buckets; b != end; ++b)
        for (char* node = *b; node; node = *reinterpret_cast<char**>(node))
            n += ListSize(reinterpret_cast<const StockList*>(node + 0x10));
    return n;
}

struct ModifierPair {
    stlp::string key;
    void* value = nullptr;
    ModifierPair(const char* first, const char* last) : key(first, last) {}
};

char* ModifierAt(ItemStock* self, const char* key, std::size_t length) {
    ModifierPair pair(key, key + length);
    char* found = ModifierMapAtForCheck(self->modifierMap, nullptr, &pair);
    return *reinterpret_cast<char**>(found + 0xC);
}

// The key length of a modifier kind: 1 for a/d/e/m/r/t, 2 for f/i, 0 for kinds left out.
std::size_t KeyLength(char kind) {
    if (kind != 0 && std::strchr("ademrt", kind)) return 1;
    if (kind == 'f' || kind == 'i') return 2;
    return 0;
}

} // namespace

static void __fastcall FillSpellMapsVerify(ItemStock* self, void*, const SpellRecipeVector* recipes) {
    self->ItemStock::FillSpellMaps(recipes);
    std::size_t expected = 0;
    for (char* r = recipes->start; r != recipes->finish; r += kSpellRecipeSize) {
        if (!(*reinterpret_cast<const std::uint32_t*>(r + 0x30) & (1u << (self->classIndex & 31)))) continue;
        char* spell = *SpellByNameForCheck(reinterpret_cast<void*>(0x007C851C), nullptr, r);
        if (!spell) continue;
        for (char** s = self->spells.start; s != self->spells.finish; ++s) {
            if (*s == spell) {
                ++expected;
                break;
            }
        }
    }
    std::size_t recipeCount = RecipeCount(self);
    bool ok = recipeCount == expected;

    // Replay the modifier rules over (key, modifier) pairs.
    struct Entry {
        char key[2];
        std::size_t length;
        char* modifier;
    };
    constexpr std::size_t kMaxKeys = 128;
    Entry entries[kMaxKeys];
    std::size_t keys = 0;
    for (char** p = self->modifiers.start; p != self->modifiers.finish; ++p) {
        char* m = *p;
        std::size_t length = KeyLength(m[4]);
        if (length == 0) continue;
        Entry* e = nullptr;
        for (std::size_t i = 0; i < keys; ++i)
            if (entries[i].length == length && std::memcmp(entries[i].key, m + 4, length) == 0) e = &entries[i];
        if (!e) {
            if (keys == kMaxKeys) continue;
            e = &entries[keys++];
            e->key[0] = m[4];
            e->key[1] = m[5];
            e->length = length;
            e->modifier = nullptr;
        }
        bool replace = length == 2 || !e->modifier ||
                       !(*reinterpret_cast<const float*>(e->modifier + 0x14) >= *reinterpret_cast<const float*>(m + 0x14));
        if (replace) e->modifier = m;
    }
    for (std::size_t i = 0; i < keys; ++i)
        if (ModifierAt(self, entries[i].key, entries[i].length) != entries[i].modifier) ok = false;
    ei::verify::Check("ItemStock::FillSpellMaps", true, ok, "class %d: %u recipes (expected %u), %u modifier keys", self->classIndex,
                      static_cast<unsigned>(recipeCount), static_cast<unsigned>(expected), static_cast<unsigned>(keys));
}
EI_VERIFY(0x0069A480, FillSpellMapsVerify, 0, s_unusedFillSpellMaps);

static void __fastcall GenerateSpellsVerify(ItemStock* self, void*, StockList* out, const float* levels, unsigned threshold) {
    std::size_t before = ListSize(out);
    std::size_t recipes = RecipeCount(self);
    self->ItemStock::GenerateSpells(out, levels, threshold);
    std::size_t after = ListSize(out);
    bool ok = after >= before && after - before <= recipes;
    std::size_t i = 0;
    for (const StockListNode* node = out->head->next; ok && node != out->head; node = node->next, ++i) {
        if (i < before) continue;
        const char* spell = static_cast<const char*>(node->value);
        if (spell && *reinterpret_cast<const int*>(spell + 4) < 1) ok = false;
    }
    ei::verify::Check("ItemStock::GenerateSpells", true, ok, "threshold %u, %u recipes: %u -> %u spells", threshold,
                      static_cast<unsigned>(recipes), static_cast<unsigned>(before), static_cast<unsigned>(after));
}
EI_VERIFY(0x0069A780, GenerateSpellsVerify, 0, s_unusedGenerateSpells);

// - LoadLists (0069B5B0; ours only: it reads the stream): every list only grew; the five pointer
//   lists gained non-null records each not listed before (no duplicates); the material vector's
//   entries before the call are unchanged. Reached when a save with class templates is loaded
//   (004A6C40 from the party manager's load; world_load / ui_saveload expected, not confirmed).
namespace {

using LoadListsFn = void(__fastcall*)(ItemStock*, void*, void*);
LoadListsFn s_unusedLoadLists;

constexpr std::size_t kMaxListed = 1024;

bool GrewUniquely(const StockPtrVector& list, std::size_t before) {
    std::size_t size = list.size();
    if (size < before) return false;
    for (std::size_t i = before; i < size; ++i) {
        if (!list.start[i]) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (list.start[j] == list.start[i]) return false;
    }
    return true;
}

} // namespace

static void __fastcall LoadListsVerify(ItemStock* self, void*, void* stream) {
    StockPtrVector* lists[5] = {&self->armors, &self->weapons, &self->quickItems, &self->spells, &self->modifiers};
    std::size_t sizes[5];
    for (int i = 0; i < 5; ++i) sizes[i] = lists[i]->size();
    std::size_t materialCount = self->materials.size();
    StockMaterial materials[kMaxListed];
    std::size_t kept = materialCount < kMaxListed ? materialCount : kMaxListed;
    for (std::size_t i = 0; i < kept; ++i) materials[i] = self->materials.start[i];
    self->ItemStock::LoadLists(stream);
    bool ok = self->materials.size() >= materialCount;
    for (int i = 0; i < 5; ++i) ok = ok && GrewUniquely(*lists[i], sizes[i]);
    for (std::size_t i = 0; ok && i < kept; ++i)
        ok = std::memcmp(&materials[i], &self->materials.start[i], sizeof(StockMaterial)) == 0;
    ei::verify::Check("ItemStock::LoadLists", true, ok, "lists %u/%u/%u/%u/%u, materials %u -> %u/%u/%u/%u/%u, %u",
                      static_cast<unsigned>(sizes[0]), static_cast<unsigned>(sizes[1]), static_cast<unsigned>(sizes[2]),
                      static_cast<unsigned>(sizes[3]), static_cast<unsigned>(sizes[4]), static_cast<unsigned>(materialCount),
                      static_cast<unsigned>(lists[0]->size()), static_cast<unsigned>(lists[1]->size()),
                      static_cast<unsigned>(lists[2]->size()), static_cast<unsigned>(lists[3]->size()),
                      static_cast<unsigned>(lists[4]->size()), static_cast<unsigned>(self->materials.size()));
}
EI_VERIFY(0x0069B5B0, LoadListsVerify, 0, s_unusedLoadLists);

// Ticket H5, all reached by scenario trader_stock (the camp opened with a constructor record):
// - ResetMaterialUnits (006993B0; from 004A6DF0): against the original. The original runs first,
//   its units and rand seed are kept, the units and seed are restored and ours runs; units and
//   seeds compared.
// - CopyMagic (00699440; from 004A6DF0; ours only, it may reallocate the vectors): the spells and
//   modifiers equal `other`'s, element for element.
// - GenerateSpellInstructions / GenerateModifierInstructions (00699C90 / 00699ED0; from 004A6F60;
//   ours only, they draw from rand and create items): one counted item per offered spell /
//   modifier, in order, +0x48 within the range, +0x54 the record's index when the record lies in
//   its table (007C8510 / 0x84, 007C8530 / 0x24; the lookup by code finds the first record with
//   that code, so a mismatch would mean duplicate codes), else -1 or a valid index;
//   the scratch vector is empty.
// - GenerateScrolls (0069AC20; from 004A6F60; ours only, ISAAC draws and new items / spells): every
//   new entry of `out` is a counted item of kind 0x3008.
namespace combatverify {
char* __cdecl GamePtd();   // verify_combat_steps.cpp; +0x14 is rand()'s seed
}

namespace {

std::uint32_t& RandSeed() { return *reinterpret_cast<std::uint32_t*>(combatverify::GamePtd() + 0x14); }

using ResetMaterialUnitsFn = void(__fastcall*)(ItemStock*, void*);
ResetMaterialUnitsFn s_originalResetMaterialUnits;
using CopyMagicFn = void(__fastcall*)(ItemStock*, void*, const ItemStock*);
CopyMagicFn s_unusedCopyMagic;
GenerateInstructionsFn s_unusedSpellInstructions;
GenerateInstructionsFn s_unusedModifierInstructions;
GenerateInstructionsFn s_unusedScrolls;

bool SameElements(const StockPtrVector& a, const StockPtrVector& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a.start[i] != b.start[i]) return false;
    return true;
}

void CheckMagicInstructions(const char* name, ItemStock* self, const StockPtrVector& source, std::uintptr_t table,
                            std::uintptr_t scratch, int recordSize, int minCount, int maxCount, StockList* out,
                            void (ItemStock::*generate)(int, int, StockList*)) {
    const char* tableStart = *reinterpret_cast<char* const*>(table);
    const char* tableEnd = *reinterpret_cast<char* const*>(table + 4);
    int tableCount = static_cast<int>((tableEnd - tableStart) / recordSize);
    std::size_t expected = source.size();
    int indices[kMaxRecords];   // -2: not in the table
    for (std::size_t i = 0; i < expected && i < kMaxRecords; ++i) {
        const char* r = source.start[i];
        indices[i] = r >= tableStart && r < tableEnd ? static_cast<int>((r - tableStart) / recordSize) : -2;
    }
    std::size_t before = ListSize(out);
    (self->*generate)(minCount, maxCount, out);
    std::size_t added = ListSize(out) - before;
    const StockPtrVector& taken = *reinterpret_cast<const StockPtrVector*>(scratch);
    int lo = minCount < maxCount ? minCount : maxCount;
    bool ok = added == expected && taken.start == taken.finish;
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
        int index = *reinterpret_cast<const int*>(item + 0x54);
        if (k < kMaxRecords && indices[k] != -2) {
            if (index != indices[k]) ok = false;
        } else if (index < -1 || index >= tableCount) {
            ok = false;
        }
    }
    ei::verify::Check(name, true, ok, "%d..%d, %u records: %u items", minCount, maxCount, static_cast<unsigned>(expected),
                      static_cast<unsigned>(added));
}

} // namespace

static void __fastcall ResetMaterialUnitsVerify(ItemStock* self, void*) {
    std::size_t count = self->materials.size();
    if (count > kMaxListed) {   // (not expected: run ours alone)
        self->ItemStock::ResetMaterialUnits();
        return;
    }
    int before[kMaxListed];
    int original[kMaxListed];
    for (std::size_t i = 0; i < count; ++i) before[i] = self->materials.start[i].left;
    std::uint32_t seed = RandSeed();
    s_originalResetMaterialUnits(self, nullptr);
    std::uint32_t originalSeed = RandSeed();
    for (std::size_t i = 0; i < count; ++i) {
        original[i] = self->materials.start[i].left;
        self->materials.start[i].left = before[i];
    }
    RandSeed() = seed;
    self->ItemStock::ResetMaterialUnits();
    int mismatch = -1;
    for (std::size_t i = 0; i < count && mismatch < 0; ++i)
        if (self->materials.start[i].left != original[i]) mismatch = static_cast<int>(i);
    ei::verify::Check("ItemStock::ResetMaterialUnits", true, mismatch < 0 && RandSeed() == originalSeed,
                      "%u entries, first mismatch %d, seed %08X / %08X", static_cast<unsigned>(count), mismatch, originalSeed,
                      RandSeed());
}
EI_VERIFY(0x006993B0, ResetMaterialUnitsVerify, 7, s_originalResetMaterialUnits);

static void __fastcall CopyMagicVerify(ItemStock* self, void*, const ItemStock* other) {
    self->ItemStock::CopyMagic(other);
    bool ok = SameElements(self->spells, other->spells) && SameElements(self->modifiers, other->modifiers);
    ei::verify::Check("ItemStock::CopyMagic", true, ok, "spells %u / %u, modifiers %u / %u", static_cast<unsigned>(self->spells.size()),
                      static_cast<unsigned>(other->spells.size()), static_cast<unsigned>(self->modifiers.size()),
                      static_cast<unsigned>(other->modifiers.size()));
}
EI_VERIFY(0x00699440, CopyMagicVerify, 0, s_unusedCopyMagic);

static void __fastcall GenerateSpellInstructionsVerify(ItemStock* self, void*, int minCount, int maxCount, StockList* out) {
    CheckMagicInstructions("ItemStock::GenerateSpellInstructions", self, self->spells, 0x007C8510, 0x007C8670, 0x84, minCount,
                           maxCount, out, &ItemStock::GenerateSpellInstructions);
}
EI_VERIFY(0x00699C90, GenerateSpellInstructionsVerify, 0, s_unusedSpellInstructions);

static void __fastcall GenerateModifierInstructionsVerify(ItemStock* self, void*, int minCount, int maxCount, StockList* out) {
    CheckMagicInstructions("ItemStock::GenerateModifierInstructions", self, self->modifiers, 0x007C8530, 0x007C8660, 0x24,
                           minCount, maxCount, out, &ItemStock::GenerateModifierInstructions);
}
EI_VERIFY(0x00699ED0, GenerateModifierInstructionsVerify, 0, s_unusedModifierInstructions);

static void __fastcall GenerateScrollsVerify(ItemStock* self, void*, int minCount, int maxCount, StockList* out) {
    std::size_t before = ListSize(out);
    self->ItemStock::GenerateScrolls(minCount, maxCount, out);
    std::size_t after = ListSize(out);
    bool ok = after >= before && NewItemsValid(out, before, 0x3008, 0x3008);
    ei::verify::Check("ItemStock::GenerateScrolls", true, ok, "%u offered spells: %u -> %u items",
                      static_cast<unsigned>(self->spells.size()), static_cast<unsigned>(before), static_cast<unsigned>(after));
}
EI_VERIFY(0x0069AC20, GenerateScrollsVerify, 0, s_unusedScrolls);
