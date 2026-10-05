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

// STLport list node / list of pointers (the list object is the sentinel pointer).
struct StockListNode {
    StockListNode* next;
    StockListNode* prev;
    void* value;
};
struct StockList {
    StockListNode* head;
};

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
    std::uint8_t spellMap[0x14];    // +0x50 spells by record (0069A480)
    std::uint8_t modifierMap[0x14]; // +0x64 modifiers by type key (0069A480)
    std::uint8_t unknown_78[0xB8 - 0x78];

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
};
static_assert(offsetof(ItemStock, materials) == 0x2C);
static_assert(offsetof(ItemStock, spellMap) == 0x50);
static_assert(sizeof(ItemStock) == 0xB8);
