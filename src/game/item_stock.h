#pragma once

// A class template's stock lists (ClassTemplate::allowed and ::magic) and the trader stock generation over
// them, run when the camp opens with the template's flag38 set: weapons, armors and quick items, then their
// instructions and materials. Each generated item goes to a list of raw CItem pointers (counted once). See
// docs/notes/items_spells.md ("Trader stock").

#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

#include "lib/stlp.h"
#include "lib/stlp_hash.h"
#include "item_db.h"
#include "game_data.h"

class CItem;
class CParty;
class CSpell;
class CStream;

using StockPrototypeVector = std::vector<itemdb::ItemRecordHead*>;
using StockSpellVector = std::vector<itemdb::SpellRecord*>;
using StockModifierVector = std::vector<itemdb::SpellModifierRecord*>;

// A material entry: the material record, the count range of a material stack, and the units left for weapons
// and armors (their generation takes the prototype's materialUnits from it).
struct StockMaterial {
    itemdb::MaterialRecordHead* record;   // (soundName its class name, unitPrice its unit price)
    int minCount;
    int maxCount;
    int left;
};
static_assert(sizeof(StockMaterial) == 0x10);

using StockMaterialVector = std::vector<StockMaterial>;

// The generated items (counted once) and the generated spells (GenerateSpells; counted while held). The recipe
// map's lists hold recipe pointers.
using StockList = std::list<CItem*>;
using StockListNode = StockList::iterator;
using StockSpellList = stlp::list<CSpell*>;
using StockSpellListNode = StockSpellList::Node;
using StockRecipeList = std::list<void*>;

using StockStringVector = std::vector<std::string>;

// A spell recipe: the spell database's list record.
using SpellRecipe = itemdb::SpellListRecord;

// A vector of spell recipes (the spell database's lists for weapons, armors and scrolls).
using SpellRecipeVector = std::vector<SpellRecipe>;

// The recipe map (ItemStock::spellMap): recipe lists by spell name.
using StockRecipeMap = stlp::hashtable<stlp::StringListTraits>;

// Records by name (ItemStock::modifierMap; the spells and modifiers by name); stlp::NamedSlot inserts a null
// record for a new name.
using RecordNameMap = stlp::NameMap;

namespace stock {
// Scratch vectors: the prototypes taken, the candidate materials of one prototype (pointers into
// the stock's material entries), the material entries taken, the spells and modifiers taken.
using CandidateVector = std::vector<StockMaterial*>;
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
// The spell database's recipe lists for weapons, armors and scrolls (FillSpellMaps' argument).
inline const SpellRecipeVector* WeaponSpellRecipes() { return &itemdb::TheSpellDatabase().lists[2]; }
inline const SpellRecipeVector* ArmorSpellRecipes() { return &itemdb::TheSpellDatabase().lists[1]; }
inline const SpellRecipeVector* ScrollSpellRecipes() { return &itemdb::TheSpellDatabase().lists[0]; }
// The spell and modifier maps by name (operator[] inserts a null record: NamedRecord).
inline itemdb::SpellTable::CodeIndex& SpellsByName() { return itemdb::TheSpellDatabase().spells.byCode; }
inline itemdb::SpellModifierTable::CodeIndex& ModifiersByName() { return itemdb::TheSpellDatabase().modifiers.byCode; }
// The material draw's factors of the prototype's price: a seventh and 0.2.
inline float SeventhFactor() { return 0.14285715f /*00743D78*/; }
inline float FifthFactor() { return 0.2f /*0073BAD4*/; }
} // namespace stock

struct ItemStock {
    CParty* context;                // the template's party object (its item constructor and item manager)
    int classIndex;                 // bit of the records' class masks
    StockPrototypeVector armors;
    StockPrototypeVector weapons;
    StockPrototypeVector quickItems;
    StockMaterialVector materials;
    StockSpellVector spells;
    StockModifierVector modifiers;
    StockRecipeMap spellMap;        // recipes by spell name
    RecordNameMap modifierMap;      // modifiers by kind key
    float baseLevels[8];            // set with scrollLevels (SetLevels)
    float scrollLevels[8];          // the spell level per school for the scrolls

    // Both maps get NextPrime(100) buckets (their functor bytes left as they are: an uninitialised stack byte
    // is copied); baseLevels and scrollLevels are not written.
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

    void SetLevels(const float* base, const float* scroll);

    // GenerateItems on weapons / armors / quick items with kind 0 / 1 / 2.
    void GenerateWeapons(int minCount, int maxCount, StockList* out);
    void GenerateArmors(int minCount, int maxCount, StockList* out);
    void GenerateQuickItems(int minCount, int maxCount, StockList* out);

    // Each material entry's `left` redrawn in [minCount, maxCount] (rand; maxCount when minCount >= maxCount).
    void ResetMaterialUnits();

    // The "allowed" stock takes the "magic" stock's spells and modifiers.
    void CopyMagic(const ItemStock* other);

    // One instruction item (kind 0x3007, the class-0x15 prototype "prototype" / "rune") per offered spell /
    // modifier, with index_ the record's index in its table (found by code; -1 when missing) and count_ a random
    // count in [minCount, maxCount], into `out`.
    void GenerateSpellInstructions(int minCount, int maxCount, StockList* out);
    void GenerateModifierInstructions(int minCount, int maxCount, StockList* out);

    // Scrolls ("spell container", kind 0x3008) of the spells the stock offers: the recipe maps for scrolls, then
    // up to 10 tries at a spell list (GenerateSpells with scrollLevels and threshold 50) until it holds at least
    // as many spells as `spells`; a scroll per spell of the last try into `out`. The counts are not used.
    void GenerateScrolls(int minCount, int maxCount, StockList* out);

    // Up to a random count in [minCount, maxCount] of the prototypes (all of them when there are not more;
    // otherwise the vector is shuffled in place and its first ones taken), each built as an item into `out`:
    // wands are skipped; a prototype with a material class gets a material of that class with enough units
    // left (a quality-weighted draw, else a random one; none at all cancels the item); weapons and armors
    // (kind 0 / 1, 3 also) get a random spell half the time when the material allows magic; potions (category
    // 8) come in stacks of 20..49.
    void GenerateItems(StockPrototypeVector* prototypes, int minCount, int maxCount, StockList* out, int kind);

    // A random count in [minCount, maxCount] of the material entries (the
    // vector shuffled in place when there are more), each as a "material" item (kind 0x3007) of a
    // random stack size in the entry's range, into `out`.
    void GenerateMaterials(int minCount, int maxCount, StockList* out);

    // One instruction item (kind 0x3007, the class-0x15 prototype "instruction weapon" / "instruction armor" /
    // "instruction quick item") per weapon / armor / quick item record of the stock (quick items named "scroll"
    // skipped), with index_ the record's index in its table and count_ a random count in [minCount, maxCount].
    void GenerateWeaponInstructions(int minCount, int maxCount, StockList* out);
    void GenerateArmorInstructions(int minCount, int maxCount, StockList* out);
    void GenerateQuickItemInstructions(int minCount, int maxCount, StockList* out);

    // Refills spellMap with the recipes for this class whose spell (by name) is offered, under their names;
    // then modifierMap with the offered modifiers under their kind letter: kinds a, d, e, m, r, t keep the
    // strongest (strength; a later one replaces unless the held one is at least as strong), f and i are keyed by
    // both letters (the last one wins), other kinds are left out.
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
    // armors, weapons, quick items (names lower-cased, unknown ones skipped); then material entries (name
    // lower-cased, kept even when unknown, and the three ints), then spells and modifiers by name as read.
    void LoadLists(CStream* stream);
    // Writes what LoadLists reads: the armor, weapon and quick item counts and names (each text with its
    // zero), the material entries (name, the three ints), the spells' and modifiers' codes.
    void SaveLists(CStream* stream);
    void Destruct();
};
static_assert(offsetof(ItemStock, materials) == 0x2C);
static_assert(offsetof(ItemStock, spellMap) == 0x50);
static_assert(offsetof(ItemStock, baseLevels) == 0x78);
static_assert(offsetof(ItemStock, scrollLevels) == 0x98);
static_assert(sizeof(ItemStock) == 0xB8);
