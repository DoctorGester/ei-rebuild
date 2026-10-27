#pragma once

// A class template's stock lists (0xB8 bytes, at ClassTemplate +0x11C "allowed" and +0x1D4 "magic"; see
// class_templates.h) and the trader stock generation over them, run when the camp opens with the template's +0x38
// flag set: weapons, armors and quick items, then their instructions and materials. Each generated item goes to a
// list of raw CItem pointers (counted once). Names are provisional; see docs/notes/items_spells.md ("Trader
// stock").

#include <cstddef>
#include <cstdint>

#include "lib/stlp.h"
#include "lib/stlp_hash.h"
#include "item_db.h"
#include "game_data.h"

class CItem;     // item.h
class CParty;    // party.h
class CSpell;    // spell.h
class CStream;   // core_streams.h

// STLport vectors of record pointers: the item prototypes (weapons, armours, quick items), the spells,
// the modifiers.
using StockPrototypeVector = stlp::vector<itemdb::ItemRecordHead*>;
using StockSpellVector = stlp::vector<itemdb::SpellRecord*>;
using StockModifierVector = stlp::vector<itemdb::SpellModifierRecord*>;

// A material entry (16 bytes, vector at +0x2C): the material record, the count range of a material stack,
// and the units left for weapons and armors (the weapon / armor generation takes the prototype's +0x4C
// units from it).
struct StockMaterial {
    itemdb::MaterialRecordHead* record;   // +0x00 (soundName its class name, unitPrice its unit price)
    int minCount;       // +0x04
    int maxCount;       // +0x08
    int left;           // +0x0C
};
static_assert(sizeof(StockMaterial) == 0x10);

using StockMaterialVector = stlp::vector<StockMaterial>;

// STLport lists of pointers (the list object is the sentinel pointer): the generated items
// (list<CItem*>, counted once) and the generated spells (list<CSpell*>, GenerateSpells; counted
// while held). The recipe map's lists hold recipe pointers (stlp::StringListPair's list<void*>).
using StockList = stlp::list<CItem*>;
using StockListNode = StockList::Node;
using StockSpellList = stlp::list<CSpell*>;
using StockSpellListNode = StockSpellList::Node;
using StockRecipeList = stlp::list<void*>;

// The spell recipes' records (0x34 bytes, inline in a SpellRecipeVector).
constexpr int kSpellRecipeSize = 0x34;

// STLport vector<string>.
using StockStringVector = stlp::vector<stlp::string>;

// A spell recipe (kSpellRecipeSize bytes, inline in a SpellRecipeVector): the spell database's
// list record (item_db.h).
using SpellRecipe = itemdb::SpellListRecord;
static_assert(sizeof(SpellRecipe) == kSpellRecipeSize);

// A vector of spell recipes (the spell database's lists for weapons, armors and scrolls).
using SpellRecipeVector = stlp::vector<SpellRecipe>;

// The recipe map (ItemStock::spellMap, hash_map<string, list<recipe*>>).
using StockRecipeMap = stlp::hashtable<stlp::StringListTraits>;
static_assert(sizeof(StockRecipeMap::Value) == 0x10);

// A map of records by name (hash_map<string, record*>; ItemStock::modifierMap, the spells and modifiers by
// name). Its operator[](name) is stlp::NamedSlot<Record>: the record slot of `name`, a null one inserted
// for a new name.
using RecordNameMap = stlp::NameMap;

// The globals of the stock generation (the exe's data).
namespace stock {
// Scratch vectors (item_stock.cpp): the prototypes taken, the candidate materials of one prototype (pointers into
// the stock's material entries), the material entries taken, the spells and modifiers taken.
using CandidateVector = stlp::vector<StockMaterial*>;
extern StockPrototypeVector g_takenPrototypes;
extern CandidateVector g_candidates;
extern StockMaterialVector g_takenMaterials;
extern StockSpellVector g_takenSpells;
extern StockModifierVector g_takenModifiers;
inline StockPrototypeVector& TakenPrototypes() { return g_takenPrototypes; }
inline CandidateVector& Candidates() { return g_candidates; }
inline StockMaterialVector& TakenMaterials() { return g_takenMaterials; }
inline StockSpellVector& TakenSpells() { return g_takenSpells; }
inline StockModifierVector& TakenModifiers() { return g_takenModifiers; }
// The spell recipe vectors for weapons, armors and scrolls (FillSpellMaps' argument).
// (The spell database's three lists of recipes: scrolls, armours and weapons.)
inline const SpellRecipeVector* WeaponSpellRecipes() { return &itemdb::TheSpellDatabase().lists[2]; }
inline const SpellRecipeVector* ArmorSpellRecipes() { return &itemdb::TheSpellDatabase().lists[1]; }
inline const SpellRecipeVector* ScrollSpellRecipes() { return &itemdb::TheSpellDatabase().lists[0]; }
// The spell and modifier maps by name (operator[] inserts a null record: NamedRecord).
inline itemdb::SpellTable::CodeIndex& SpellsByName() { return itemdb::TheSpellDatabase().spells.byCode; }
inline itemdb::SpellModifierTable::CodeIndex& ModifiersByName() { return itemdb::TheSpellDatabase().modifiers.byCode; }
// The material draw's factors of the prototype's price (read from the exe's data): a seventh and 0.2.
inline float SeventhFactor() { return 0.14285715f /*00743D78*/; }
inline float FifthFactor() { return 0.2f /*0073BAD4*/; }
} // namespace stock

struct ItemStock {
    CParty* context;                // +0x00 the template's party object (its item constructor and item manager)
    int classIndex;                 // +0x04 bit of the records' class masks
    StockPrototypeVector armors;    // +0x08
    StockPrototypeVector weapons;   // +0x14
    StockPrototypeVector quickItems;   // +0x20
    StockMaterialVector materials;  // +0x2C
    StockSpellVector spells;        // +0x38
    StockModifierVector modifiers;  // +0x44
    StockRecipeMap spellMap;        // +0x50 recipes by spell name
    RecordNameMap modifierMap;      // +0x64 modifiers by kind key
    float baseLevels[8];            // +0x78 8 floats, set with +0x98
    float scrollLevels[8];          // +0x98 the spell level per school for the scrolls

    // Constructor body: `context`, class index 0, the six vectors empty, both maps empty with
    // NextPrime(100) buckets (their functor bytes left as they are; an uninitialised stack byte is copied).
    // Returns this. +0x78..+0xB7 are not written.
    ItemStock* Construct(CParty* context);

    // `record` appended to the armors / weapons / quick items / spells / modifiers unless null or already
    // there.
    void AddArmor(itemdb::ArmorRecord* record);
    void AddWeapon(itemdb::WeaponRecord* record);
    void AddQuickItem(itemdb::QuickItemRecord* record);
    void AddSpell(itemdb::SpellRecord* record);
    void AddModifier(itemdb::SpellModifierRecord* record);
    // The material entry of `record` (null: nothing) gets the count range and `maxCount` units left; a new
    // entry is appended when the record has none.
    void AddMaterial(itemdb::MaterialRecordHead* record, int minCount, int maxCount);

    // +0x78 and +0x98 each set from 8 floats.
    void SetLevels(const float* base, const float* scroll);

    // Thunks onto the common generator with +0x14 weapons / +8 armors / +0x20 quick items and kind 0 / 1 / 2.
    void GenerateWeapons(int minCount, int maxCount, StockList* out);
    void GenerateArmors(int minCount, int maxCount, StockList* out);
    void GenerateQuickItems(int minCount, int maxCount, StockList* out);

    // Each material entry's units left (+0x0C) redrawn in [minCount, maxCount] (rand;
    // maxCount when minCount >= maxCount).
    void ResetMaterialUnits();

    // The spells (+0x38) and modifiers (+0x44) assigned from `other` ( the "allowed" stock takes the
    // "magic" stock's).
    void CopyMagic(const ItemStock* other);

    // One instruction item (kind 0x3007, the class-0x15 prototype "prototype" / "rune") per offered spell
    // (+0x38) / modifier (+0x44), with +0x54 the record's index in its table (found by code; -1 when
    // missing) and +0x48 a random count in [minCount, maxCount], into `out`.
    void GenerateSpellInstructions(int minCount, int maxCount, StockList* out);
    void GenerateModifierInstructions(int minCount, int maxCount, StockList* out);

    // Scrolls ("spell container", kind 0x3008) of the spells the stock offers: the recipe maps for scrolls,
    // then up to 10 tries at a spell list (the spell-list generator with +0x98 and threshold 50) until it
    // holds at least as many spells as the stock offers (+0x38); a scroll per spell of the last try into
    // `out`. The counts are not used.
    void GenerateScrolls(int minCount, int maxCount, StockList* out);

    // Up to a random count in [minCount, maxCount] of the prototypes (all of
    // them when there are not more; otherwise the vector is shuffled in place and its first ones
    // taken), each built as an item into `out`: wands are skipped; a prototype with a material
    // class gets a material of that class with enough units left (a quality-weighted draw, else a
    // random one; none at all cancels the item); weapons and armors (kind 0 / 1, 3 also) get a
    // random spell half the time when the material allows magic; potions (category 8) come in
    // stacks of 20..49.
    void GenerateItems(StockPrototypeVector* prototypes, int minCount, int maxCount, StockList* out, int kind);

    // A random count in [minCount, maxCount] of the material entries (the
    // vector shuffled in place when there are more), each as a "material" item (kind 0x3007) of a
    // random stack size in the entry's range, into `out`.
    void GenerateMaterials(int minCount, int maxCount, StockList* out);

    // One instruction item (kind 0x3007, the class-0x15 prototype "instruction weapon" / "instruction
    // armor" / "instruction quick item") per weapon / armor / quick item record of the stock (quick items
    // named "scroll" skipped), with +0x54 the record's index in its table and +0x48 a random count in
    // [minCount, maxCount], into `out`.
    void GenerateWeaponInstructions(int minCount, int maxCount, StockList* out);
    void GenerateArmorInstructions(int minCount, int maxCount, StockList* out);
    void GenerateQuickItemInstructions(int minCount, int maxCount, StockList* out);

    // Refills the recipe map (+0x50) with the recipes for this class whose spell (by name) is offered
    // (+0x38), under their names; then the modifier map (+0x64) with the offered modifiers (+0x44) under
    // their kind letter: kinds a, d, e, m, r, t keep the strongest (+0x14; a later one replaces unless the
    // held one is at least as strong), f and i are keyed by both letters (the last one wins), other kinds
    // are left out.
    void FillSpellMaps(const SpellRecipeVector* recipes);

    // For each recipe in the map, unless an ISAAC draw % 101 is below `threshold`: the spell's prototype
    // with its required modifiers (those missing from the modifier map left out), kept only when their
    // level is within the budget (the prototype school's entry of `levels`, rounded) and they are at most
    // 8; then each optional modifier, while fewer than 8, half the time (ISAAC) when it fits the budget;
    // then the repeated modifier while it fits and fewer than 8 (a kind 'm' one scales the cost and stops
    // once the cost is not above the base cost). The spell built from them (may be null) is appended,
    // counted, to `out`.
    void GenerateSpells(StockSpellList* out, const float* levels, unsigned threshold);

    // Reads five counted name lists from `stream` and appends the records they name, unless already listed:
    // armors (+8), weapons (+0x14), quick items (+0x20), names lower-cased, unknown ones skipped; then
    // material entries (+0x2C: name lower-cased (kept even when unknown) and the three ints), then spells
    // (+0x38) and modifiers (+0x44) by name as read.
    void LoadLists(CStream* stream);
    // Writes what LoadLists reads: the armor, weapon and quick item counts and names (each text with its
    // zero), the material entries (name, the three ints), the spells' and modifiers' codes.
    void SaveLists(CStream* stream);
    // The destructor as the class template's inlines it: context cleared, the modifier and spell maps
    // destroyed, each vector's storage freed from the last to the first.
    void Destruct();
};
static_assert(offsetof(ItemStock, materials) == 0x2C);
static_assert(offsetof(ItemStock, spellMap) == 0x50);
static_assert(offsetof(ItemStock, baseLevels) == 0x78);
static_assert(offsetof(ItemStock, scrollLevels) == 0x98);
static_assert(sizeof(ItemStock) == 0xB8);
