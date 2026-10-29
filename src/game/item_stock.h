#pragma once

// A class template's stock lists (ClassTemplate::allowed and ::magic) and the trader stock generation over
// them, run when the camp opens with the template's restockDue set: weapons, armors and quick items, then their
// instructions and materials. Each generated item goes to a list of raw Item pointers (counted once). See
// docs/notes/items_spells.md ("Trader stock").

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "engine_object.h"
#include "item_db.h"
#include "lib/hash_map.h"

class Item;
class CParty;
class CSpell;
class ByteStream;

// A material entry: the material record, the count range of a material stack, and the units left for weapons
// and armors (their generation takes the prototype's materialUnits from it).
struct StockMaterial {
    itemdb::MaterialRecordHead* record;   // (soundName its class name, unitPrice its unit price)
    int minCount;
    int maxCount;
    int left;
};

// The recipe map (ItemStock::spellMap): recipe lists by spell name, walked in STLport's order (the spells are
// offered in it, with the random rolls).
using StockRecipeMap = lib::HashMap<std::string, std::vector<const itemdb::SpellListRecord*>, lib::TextHash>;

namespace stock {
// The material draw's factors of the prototype's price: a seventh and a fifth.
inline constexpr float kSeventhFactor = 0.14285715f;
inline constexpr float kFifthFactor = 0.2f;
// The most modifiers a generated spell takes.
inline constexpr int kMaxSpellModifiers = 8;
} // namespace stock

struct ItemStock {
    // The owner `party` and class index 0; baseLevels and scrollLevels are not written.
    explicit ItemStock(CParty* party) : party(party) {}

    CParty* party;                  // the template's party object (its item constructor and item manager)
    int classIndex = 0;             // bit of the records' class masks
    std::vector<itemdb::ItemRecordHead*> armors;
    std::vector<itemdb::ItemRecordHead*> weapons;
    std::vector<itemdb::ItemRecordHead*> quickItems;
    std::vector<StockMaterial> materials;
    std::vector<itemdb::SpellRecord*> spells;
    std::vector<itemdb::SpellModifierRecord*> modifiers;
    StockRecipeMap spellMap;        // recipes by spell name
    std::unordered_map<std::string, itemdb::SpellModifierRecord*> modifierMap;      // modifiers by kind key
    float baseLevels[kSchoolCount];     // per school; set with scrollLevels (SetLevels)
    float scrollLevels[kSchoolCount];   // the spell level per school for the scrolls

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

    // GenerateItems on weapons (with the weapon spell recipes) / armors (armour recipes) / quick items (no
    // spells).
    void GenerateWeapons(int minCount, int maxCount, std::vector<Item*>* out);
    void GenerateArmors(int minCount, int maxCount, std::vector<Item*>* out);
    void GenerateQuickItems(int minCount, int maxCount, std::vector<Item*>* out);

    // Each material entry's `left` redrawn in [minCount, maxCount] (rand; maxCount when minCount >= maxCount).
    void ResetMaterialUnits();

    // The "allowed" stock takes the "magic" stock's spells and modifiers.
    void CopyMagic(const ItemStock* other);

    // One instruction item (kind ItemKind::Loot, the loot prototype "prototype" / "rune") per offered spell /
    // modifier, with index the record's index in its table (found by code; -1 when missing) and count a random
    // count in [minCount, maxCount], into `out`.
    void GenerateSpellInstructions(int minCount, int maxCount, std::vector<Item*>* out);
    void GenerateModifierInstructions(int minCount, int maxCount, std::vector<Item*>* out);

    // Scrolls ("spell container", kind ItemKind::Scroll) of the spells the stock offers: the recipe maps for scrolls, then
    // up to 10 tries at a spell list (GenerateSpells with scrollLevels and threshold 50) until it holds at least
    // as many spells as `spells`; a scroll per spell of the last try into `out`.
    void GenerateScrolls(std::vector<Item*>* out);

    // Up to a random count in [minCount, maxCount] of the prototypes (all of them when there are not more;
    // otherwise the vector is shuffled in place and its first ones taken), each built as an item into `out`:
    // wands are skipped; a prototype with a material class gets a material of that class with enough units
    // left (a quality-weighted draw, else a random one; none at all cancels the item); with `spellRecipes`
    // (weapons and armours) the item gets a random spell half the time when the material allows magic; potions
    // come in stacks of 20..49.
    void GenerateItems(std::vector<itemdb::ItemRecordHead*>* prototypes, int minCount, int maxCount,
                       std::vector<Item*>* out, const std::vector<itemdb::SpellListRecord>* spellRecipes);

    // A random count in [minCount, maxCount] of the material entries (the
    // vector shuffled in place when there are more), each as a "material" item (kind ItemKind::Loot) of a
    // random stack size in the entry's range, into `out`.
    void GenerateMaterials(int minCount, int maxCount, std::vector<Item*>* out);

    // One instruction item (kind ItemKind::Loot, the loot prototype "instruction weapon" / "instruction armor" /
    // "instruction quick item") per weapon / armor / quick item record of the stock (quick items of the family
    // "scroll" skipped), with index the record's index in its table and count a random count in [minCount,
    // maxCount].
    void GenerateWeaponInstructions(int minCount, int maxCount, std::vector<Item*>* out);
    void GenerateArmorInstructions(int minCount, int maxCount, std::vector<Item*>* out);
    void GenerateQuickItemInstructions(int minCount, int maxCount, std::vector<Item*>* out);

    // Refills spellMap with the recipes for this class whose spell (by name) is offered, under their names;
    // then modifierMap with the offered modifiers under their kind letter: kinds a, d, e, m, r, t keep the
    // strongest (strength; a later one replaces unless the held one is at least as strong), f and i are keyed by
    // both letters (the last one wins), other kinds are left out.
    void FillSpellMaps(const std::vector<itemdb::SpellListRecord>& recipes);

    // For each recipe in the map, unless an ISAAC draw % 101 is below `threshold`: the spell's prototype
    // with its required modifiers (those missing from the modifier map left out), kept only when their
    // level is within the budget (the prototype school's entry of `levels`, rounded) and they are at most
    // kMaxSpellModifiers; then each optional modifier, while fewer, half the time (ISAAC) when it fits the
    // budget; then the repeated modifier while it fits and fewer (a kind 'm' one scales the cost and stops
    // once the cost is not above the base cost). The spells built from them (may be null), held.
    std::vector<HeldRef<CSpell>> GenerateSpells(const float* levels, unsigned threshold);

    // Reads five counted name lists from `stream` and appends the records they name, unless already listed:
    // armors, weapons, quick items (names lower-cased, unknown ones skipped); then material entries (name
    // lower-cased, kept even when unknown, and the three ints), then spells and modifiers by name as read.
    void LoadLists(ByteStream* stream);
    // Writes what LoadLists reads: the armor, weapon and quick item counts and names (each text with its
    // zero), the material entries (name, the three ints), the spells' and modifiers' codes.
    void SaveLists(ByteStream* stream);
};
