#pragma once

// A class template's stock lists (0xB8 bytes, at ClassTemplate +0x11C "allowed" and +0x1D4 "magic";
// see class_templates.h) and the trader stock generation over them (0069A110-0069B544), run when
// the camp opens with the template's +0x38 flag set (004A6DF0, from the camp's 0060DD2E): weapons,
// armors and quick items (0069AE20 through 0069B570 / 0069B550 / 0069B590), then their
// instructions (00699810 / 006995E0 / 00699A40) and materials (0069A110). Each generated item goes to a
// list of raw CItem pointers (counted once). Names are provisional; see docs/notes/items_spells.md
// ("Trader stock").

#include <cstddef>
#include <cstdint>

#include "lib/stlp.h"

// STLport vector of pointers (start, finish, end of storage).
struct StockPtrVector {
    char** start;
    char** finish;
    char** eos;

    std::size_t size() const { return static_cast<std::size_t>(finish - start); }
};

// A material entry (16 bytes, vector at +0x2C): the material record, the count range of a material
// stack (0069A110), and the units left for weapons and armors (0069AE20 takes the prototype's
// +0x4C units from it).
struct StockMaterial {
    char* record;       // +0x00 material record (+0x04 its class name, +0x10 its quality)
    int minCount;       // +0x04
    int maxCount;       // +0x08
    int left;           // +0x0C
};
static_assert(sizeof(StockMaterial) == 0x10);

struct StockMaterialVector {
    StockMaterial* start;
    StockMaterial* finish;
    StockMaterial* eos;

    std::size_t size() const { return static_cast<std::size_t>(finish - start); }
};

// STLport list of pointers (the list object is the sentinel pointer).
using StockList = stlp::list<void*>;
using StockListNode = StockList::Node;

// A vector of spell recipes (0x34-byte records inline; 007C8568 for weapons, 007C855C for armors):
// +0x00 the spell's name (STLport string), +0x0C / +0x18 the required / optional modifier names
// (vectors of strings), +0x24 the modifier repeated while it fits, +0x30 the classes it suits (bit
// per ItemStock::classIndex).
constexpr int kSpellRecipeSize = 0x34;
struct SpellRecipeVector {
    char* start;
    char* finish;
    char* eos;
};

// STLport vector<string> (start, finish, end of storage).
struct StockStringVector {
    const stlp::string* start;
    const stlp::string* finish;
    const stlp::string* eos;
    unsigned size() const { return static_cast<unsigned>(finish - start); }
};

// A spell recipe (kSpellRecipeSize bytes, inline in a SpellRecipeVector).
struct SpellRecipe {
    stlp::string spell;             // +0x00 the spell's name
    StockStringVector required;     // +0x0C modifier names
    StockStringVector optional;     // +0x18 modifier names
    stlp::string repeated;          // +0x24 the modifier repeated while it fits
    std::uint32_t classMask;        // +0x30 bit per ItemStock::classIndex
};
static_assert(offsetof(SpellRecipe, required) == 0x0C);
static_assert(offsetof(SpellRecipe, repeated) == 0x24);
static_assert(offsetof(SpellRecipe, classMask) == 0x30);
static_assert(sizeof(SpellRecipe) == kSpellRecipeSize);

// A pair of the recipe map (ItemStock::spellMap, hash_map<string, list<recipe*>>) and its node.
struct StockRecipeEntry {
    stlp::string name;              // +0x00
    StockList recipes;              // +0x0C (its sentinel)
};
struct StockRecipeNode {
    StockRecipeNode* next;          // +0x00
    StockRecipeEntry entry;         // +0x04 (the list at +0x10)
};
static_assert(offsetof(StockRecipeNode, entry) == 0x04);
static_assert(sizeof(StockRecipeEntry) == 0x10);

// A pair of the modifier map (ItemStock::modifierMap, map<string, modifier*>; 0x10 bytes): the
// key and the modifier record (itemdb::SpellModifierRecord).
struct StockModifierEntry {
    stlp::string key;
    char* modifier;                 // +0x0C
};
static_assert(sizeof(StockModifierEntry) == 0x10);

// The globals of the stock generation (the exe's data).
namespace stock {
// Scratch vectors: the prototypes taken (007C8680), the candidate materials of one prototype
// (007C8650, pointers into the stock's material entries), the material entries taken (007C8640),
// the spells (007C8670) and modifiers (007C8660) taken.
struct CandidateVector {
    StockMaterial** start;
    StockMaterial** finish;
    StockMaterial** eos;
};
inline StockPtrVector& TakenPrototypes() { return *reinterpret_cast<StockPtrVector*>(0x007C8680); }
inline CandidateVector& Candidates() { return *reinterpret_cast<CandidateVector*>(0x007C8650); }
inline StockMaterialVector& TakenMaterials() { return *reinterpret_cast<StockMaterialVector*>(0x007C8640); }
inline StockPtrVector& TakenSpells() { return *reinterpret_cast<StockPtrVector*>(0x007C8670); }
inline StockPtrVector& TakenModifiers() { return *reinterpret_cast<StockPtrVector*>(0x007C8660); }
// The spell recipe vectors for weapons, armors and scrolls (FillSpellMaps' argument).
inline void* WeaponSpellRecipes() { return reinterpret_cast<void*>(0x007C8568); }
inline void* ArmorSpellRecipes() { return reinterpret_cast<void*>(0x007C855C); }
inline void* ScrollSpellRecipes() { return reinterpret_cast<void*>(0x007C8550); }
// The spell and modifier maps by name (STLport maps; their operator[] inserts a null record).
inline void* SpellsByName() { return reinterpret_cast<void*>(0x007C851C); }
inline void* ModifiersByName() { return reinterpret_cast<void*>(0x007C853C); }
// The material draw's factors of the prototype's price (read from the exe as the original does):
// 00743D78 (a seventh) and 0073BAD4 (0.2).
inline float SeventhFactor() { return *reinterpret_cast<const float*>(0x00743D78); }
inline float FifthFactor() { return *reinterpret_cast<const float*>(0x0073BAD4); }
} // namespace stock

struct ItemStock {
    void* context;                  // +0x00 the template's party object: slot 16 the item constructor,
                                    //   slot 18 the item manager
    int classIndex;                 // +0x04 bit of the records' class masks
    StockPtrVector armors;          // +0x08
    StockPtrVector weapons;         // +0x14
    StockPtrVector quickItems;      // +0x20
    StockMaterialVector materials;  // +0x2C
    StockPtrVector spells;          // +0x38
    StockPtrVector modifiers;       // +0x44
    std::uint8_t spellMap[0x14];    // +0x50 recipes by spell name (STLport hash_map<string, list>, 0069A480)
    std::uint8_t modifierMap[0x14]; // +0x64 modifiers by kind key (STLport map<string, modifier*>, 0069A480)
    float baseLevels[8];            // +0x78 8 floats, set with +0x98 (00699410)
    float scrollLevels[8];          // +0x98 the spell level per school for the scrolls (0069AC20)

    // 004A66C0 (RET 4; class_template_stock.cpp). Constructor body: `context`, class index 0, the
    // six vectors empty, both maps empty with NextPrime(100) buckets (their functor bytes left as
    // they are; the original copies an uninitialised stack byte). Returns this. +0x78..+0xB7 are
    // not written.
    ItemStock* Construct(void* context);

    // 00699410 (RET 8). +0x78 and +0x98 each set from 8 floats (004A6DC0).
    void SetLevels(const float* base, const float* scroll);

    // 0069B570 / 0069B550 / 0069B590 (RET 0xC; thunks onto 0069AE20 with +0x14 weapons / +8 armors /
    // +0x20 quick items and kind 0 / 1 / 2).
    void GenerateWeapons(int minCount, int maxCount, StockList* out);
    void GenerateArmors(int minCount, int maxCount, StockList* out);
    void GenerateQuickItems(int minCount, int maxCount, StockList* out);

    // 006993B0. Each material entry's units left (+0x0C) redrawn in [minCount, maxCount] (rand;
    // maxCount when minCount >= maxCount).
    void ResetMaterialUnits();

    // 00699440 (RET 4). The spells (+0x38) and modifiers (+0x44) assigned from `other` (004A6DF0:
    // the "allowed" stock takes the "magic" stock's).
    void CopyMagic(const ItemStock* other);

    // 00699C90 / 00699ED0 (RET 0xC). One instruction item (kind 0x3007, the class-0x15 prototype
    // "prototype" / "rune") per offered spell (+0x38) / modifier (+0x44), with +0x54 the record's
    // index in its table (found by code, 0068C820 / 0068C920; -1 when missing) and +0x48 a random
    // count in [minCount, maxCount], into `out`.
    void GenerateSpellInstructions(int minCount, int maxCount, StockList* out);
    void GenerateModifierInstructions(int minCount, int maxCount, StockList* out);

    // 0069AC20 (RET 0xC; from 004A6F60). Scrolls ("spell container", kind 0x3008) of the spells the
    // stock offers: the recipe maps for scrolls (007C8550), then up to 10 tries at a spell list
    // (0069A780 with +0x98 and threshold 50) until it holds at least as many spells as the stock
    // offers (+0x38); a scroll per spell of the last try into `out`. The counts are not used.
    void GenerateScrolls(int minCount, int maxCount, StockList* out);

    // 0069AE20 (RET 0x14). Up to a random count in [minCount, maxCount] of the prototypes (all of
    // them when there are not more; otherwise the vector is shuffled in place and its first ones
    // taken), each built as an item into `out`: wands are skipped; a prototype with a material
    // class gets a material of that class with enough units left (a quality-weighted draw, else a
    // random one; none at all cancels the item); weapons and armors (kind 0 / 1, 3 also) get a
    // random spell half the time when the material allows magic; potions (category 8) come in
    // stacks of 20..49.
    void GenerateItems(StockPtrVector* prototypes, int minCount, int maxCount, StockList* out, int kind);

    // 0069A110 (RET 0xC). A random count in [minCount, maxCount] of the material entries (the
    // vector shuffled in place when there are more), each as a "material" item (kind 0x3007) of a
    // random stack size in the entry's range, into `out`.
    void GenerateMaterials(int minCount, int maxCount, StockList* out);

    // 00699810 / 006995E0 / 00699A40 (RET 0xC). One instruction item (kind 0x3007, the class-0x15
    // prototype "instruction weapon" / "instruction armor" / "instruction quick item") per weapon /
    // armor / quick item record of the stock (quick items named "scroll" skipped), with +0x54 the
    // record's index in its table and +0x48 a random count in [minCount, maxCount], into `out`.
    void GenerateWeaponInstructions(int minCount, int maxCount, StockList* out);
    void GenerateArmorInstructions(int minCount, int maxCount, StockList* out);
    void GenerateQuickItemInstructions(int minCount, int maxCount, StockList* out);

    // 0069A480 (RET 4). Refills the recipe map (+0x50) with the recipes for this class whose spell
    // (007C851C by name) is offered (+0x38), under their names; then the modifier map (+0x64) with
    // the offered modifiers (+0x44) under their kind letter: kinds a, d, e, m, r, t keep the
    // strongest (+0x14; a later one replaces unless the held one is at least as strong), f and i
    // are keyed by both letters (the last one wins), other kinds are left out.
    void FillSpellMaps(const SpellRecipeVector* recipes);

    // 0069A780 (RET 0xC). For each recipe in the map, unless an ISAAC draw % 101 is below
    // `threshold`: the spell's prototype (007C8510) with its required modifiers (those missing
    // from the modifier map left out), kept only when their level is within the budget (the
    // prototype school's entry of `levels`, rounded) and they are at most 8; then each optional
    // modifier, while fewer than 8, half the time (ISAAC) when it fits the budget; then the
    // repeated modifier while it fits and fewer than 8 (a kind 'm' one scales the cost and stops
    // once the cost is not above the base cost). The spell built from them (00689310, may be null)
    // is appended, counted, to `out`.
    void GenerateSpells(StockList* out, const float* levels, unsigned threshold);

    // 0069B5B0 (RET 4; from the class template serializer 004A6C40 when loading). Reads five
    // counted name lists from `stream` (a CStream) and appends the records they name, unless
    // already listed: armors (+8, 004AB820), weapons (+0x14, 004AB720), quick items (+0x20,
    // 00613820), names lower-cased, unknown ones skipped; then material entries (+0x2C: name
    // lower-cased (0069D290, kept even when unknown) and the three ints), then spells (+0x38) and
    // modifiers (+0x44) by name as read (maps 007C851C / 007C853C).
    void LoadLists(void* stream);
    // 0069BAC0 (RET 4; not rewritten). Writes what LoadLists reads.
    void SaveLists(void* stream);
    // The destructor as the class template's (004A6A90) inlines it: context cleared, the modifier
    // and spell maps destroyed (their own instantiations, 004A9010 / 004A8E60), each vector's storage
    // freed from the last to the first.
    void Destruct();
};
static_assert(offsetof(ItemStock, materials) == 0x2C);
static_assert(offsetof(ItemStock, spellMap) == 0x50);
static_assert(offsetof(ItemStock, baseLevels) == 0x78);
static_assert(offsetof(ItemStock, scrollLevels) == 0x98);
static_assert(sizeof(ItemStock) == 0xB8);
