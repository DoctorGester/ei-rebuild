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
#include "lib/stlp_hash.h"
#include "item_db.h"
#include "game_data.h"

class CStream;   // core_streams.h

// STLport vector of record pointers.
using StockPtrVector = stlp::vector<char*>;

// A material entry (16 bytes, vector at +0x2C): the material record, the count range of a material
// stack (0069A110), and the units left for weapons and armors (0069AE20 takes the prototype's
// +0x4C units from it).
struct StockMaterial {
    char* record;       // +0x00 material record (+0x04 its class name, +0x10 its unit price)
    int minCount;       // +0x04
    int maxCount;       // +0x08
    int left;           // +0x0C
};
static_assert(sizeof(StockMaterial) == 0x10);

using StockMaterialVector = stlp::vector<StockMaterial>;

// STLport list of pointers (the list object is the sentinel pointer).
using StockList = stlp::list<void*>;
using StockListNode = StockList::Node;

// The spell recipes' records (0x34 bytes, inline in a SpellRecipeVector).
constexpr int kSpellRecipeSize = 0x34;

// STLport vector<string>.
using StockStringVector = stlp::vector<stlp::string>;

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

// A vector of spell recipes (007C8568 for weapons, 007C855C for armors, 007C8550 for scrolls).
using SpellRecipeVector = stlp::vector<SpellRecipe>;

// The recipe map (ItemStock::spellMap, hash_map<string, list<recipe*>>).
using StockRecipeMap = stlp::hashtable<stlp::StringListTraits>;
static_assert(sizeof(StockRecipeMap::Value) == 0x10);

// A map of records by name (hash_map<string, record*>; ItemStock::modifierMap, the spells and
// modifiers by name).
using RecordNameMap = stlp::NameMap;
// operator[](name) (e.g. 004ABD00, 00684B20, 0069D1E0): the record slot of `name`, a null one
// inserted for a new name.
inline char*& NamedRecord(RecordNameMap& map, const stlp::string& name) { return stlp::NamedSlot<char>(map, name); }

// The globals of the stock generation (the exe's data).
namespace stock {
// Scratch vectors (item_stock.cpp): the prototypes taken, the candidate materials of one prototype (pointers into
// the stock's material entries), the material entries taken, the spells and modifiers taken.
using CandidateVector = stlp::vector<StockMaterial*>;
extern StockPtrVector g_takenPrototypes;
extern CandidateVector g_candidates;
extern StockMaterialVector g_takenMaterials;
extern StockPtrVector g_takenSpells;
extern StockPtrVector g_takenModifiers;
inline StockPtrVector& TakenPrototypes() { return g_takenPrototypes; }
inline CandidateVector& Candidates() { return g_candidates; }
inline StockMaterialVector& TakenMaterials() { return g_takenMaterials; }
inline StockPtrVector& TakenSpells() { return g_takenSpells; }
inline StockPtrVector& TakenModifiers() { return g_takenModifiers; }
// The spell recipe vectors for weapons, armors and scrolls (FillSpellMaps' argument).
// (The spell database's three lists of recipes: scrolls, armours and weapons.)
inline const SpellRecipeVector* WeaponSpellRecipes() {
    return reinterpret_cast<const SpellRecipeVector*>(&itemdb::TheSpellDatabase().lists[2]);
}
inline const SpellRecipeVector* ArmorSpellRecipes() {
    return reinterpret_cast<const SpellRecipeVector*>(&itemdb::TheSpellDatabase().lists[1]);
}
inline const SpellRecipeVector* ScrollSpellRecipes() {
    return reinterpret_cast<const SpellRecipeVector*>(&itemdb::TheSpellDatabase().lists[0]);
}
// The spell and modifier maps by name (operator[] inserts a null record: NamedRecord).
inline RecordNameMap& SpellsByName() { return itemdb::NameMapOf(itemdb::TheSpellDatabase().spells); }
inline RecordNameMap& ModifiersByName() { return itemdb::NameMapOf(itemdb::TheSpellDatabase().modifiers); }
// The material draw's factors of the prototype's price (read from the exe as the original does):
// 00743D78 (a seventh) and 0073BAD4 (0.2).
inline float SeventhFactor() { return 0.14285715f /*00743D78*/; }
inline float FifthFactor() { return 0.2f /*0073BAD4*/; }
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
    StockRecipeMap spellMap;        // +0x50 recipes by spell name (0069A480)
    RecordNameMap modifierMap;      // +0x64 modifiers by kind key (0069A480)
    float baseLevels[8];            // +0x78 8 floats, set with +0x98 (00699410)
    float scrollLevels[8];          // +0x98 the spell level per school for the scrolls (0069AC20)

    // 004A66C0 (RET 4; class_template_stock.cpp). Constructor body: `context`, class index 0, the
    // six vectors empty, both maps empty with NextPrime(100) buckets (their functor bytes left as
    // they are; the original copies an uninitialised stack byte). Returns this. +0x78..+0xB7 are
    // not written.
    ItemStock* Construct(void* context);

    // 00699080 / 006990E0 / 00699140 / 006991A0 / 00699200 (RET 4; A-H25). `record` appended to the
    // armors / weapons / quick items / spells / modifiers unless null or already there.
    void AddArmor(char* record);
    void AddWeapon(char* record);
    void AddQuickItem(char* record);
    void AddSpell(char* record);
    void AddModifier(char* record);
    // 00699260 (RET 0xC; A-H25). The material entry of `record` (null: nothing) gets the count range
    // and `maxCount` units left; a new entry is appended when the record has none.
    void AddMaterial(char* record, int minCount, int maxCount);

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
    // counted name lists from `stream` and appends the records they name, unless
    // already listed: armors (+8, 004AB820), weapons (+0x14, 004AB720), quick items (+0x20,
    // 00613820), names lower-cased, unknown ones skipped; then material entries (+0x2C: name
    // lower-cased (0069D290, kept even when unknown) and the three ints), then spells (+0x38) and
    // modifiers (+0x44) by name as read (maps 007C851C / 007C853C).
    void LoadLists(CStream* stream);
    // 0069BAC0 (RET 4; A-H17; from the class template serializer 004A6C40 when saving). Writes
    // what LoadLists reads: the armor, weapon and quick item counts and names (each text with its
    // zero), the material entries (name, the three ints), the spells' and modifiers' codes.
    void SaveLists(CStream* stream);
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
