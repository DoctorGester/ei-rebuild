#pragma once

// Class templates (0x28C bytes, a counted object): one per
// hero class a new game offers, each with the weapons, armors, items, spells and modifiers that class
// may use (the records' class masks). Built at new game into the vector at +0x38 of the
// party manager object. Names are provisional.

#include "core_refcount.h"
#include "item_stock.h"
#include "party_storage.h"
#include "lib/stlp.h"

class CParty;   // party.h
struct PartyManager;   // party_manager.h
class ChunkArchive;

#include <cstddef>
#include <cstdint>

class ClassTemplate : public CRefCounted {
public:
    // Construct (class_template_stock.cpp; the count is the creator's): the two parties (CParty's
    // constructor), the stocks (ItemStock::Construct on the items / spells party; the first one
    // inlined there): both parties created (slot 2), class -1, both flags 0, flag38 1.
    // weightsTail is zeroed too (the game never sets it, and saves it).
    ClassTemplate();
    // The deleting destructor (class_template_stock.cpp): both parties destroyed (slot 3), the
    // stocks' maps and vectors freed (the magic stock first, each stock's context cleared), both
    // parties' destructors (spells first).
    ~ClassTemplate() override;
    // Serialize (slot 1; party_manager.cpp). Fields 1 class (4 bytes), 2 / 3 the flags (1 byte
    // each), 4 the weights and the 4 bytes after them (0x28), stream field 5 (both parties without
    // variables, then both stocks' lists: SaveLists / LoadLists), field 6 flag38
    // (1 byte).
    void Serialize(ChunkArchive* ar) override;
    // The constructor run on `this` (the registration's; the harness's zeroing patch calls it,
    // tests/verify/harness_leftovers.cpp). Returns this.
    ClassTemplate* Construct();
    // (class_template_stock.cpp). Both stocks take the 8 base and 8 scroll
    // levels (ItemStock::SetLevels).
    void SetLevels(const float* base, const float* scroll);

    CParty* ItemsParty() { return items.Get(); }
    CParty* SpellsParty() { return spells.Get(); }

    int type;                       // +0x08 class 1..5; bit (type - 1) of the records' class masks
    std::uint8_t flag0C;            // +0x0C
    std::uint8_t flag0D;            // +0x0D
    std::uint8_t unused_0E[2];
    float weights[9];               // +0x10..+0x30
    std::uint8_t weightsTail[4];        // +0x34 field 4's tenth dword (archived with weights)
    std::uint8_t flag38;            // +0x38 set by the constructor and by a quest completion
    std::uint8_t unused_39[3];
    // +0x3C / +0xAC the party objects (CParty; party_storage.h): ItemsParty /
    // SpellsParty.
    PartyStorage items;
    PartyStorage spells;
    ItemStock allowed;              // +0x11C everything the class may use (vectors at +0x08 armors,
                                    //   +0x14 weapons, +0x20 quick items, +0x2C the fourth kind
                                    //   (16-byte entries), +0x38 spells, +0x44 modifiers)
    ItemStock magic;                // +0x1D4 the same layout: spells and modifiers only
};
static_assert(sizeof(ClassTemplate) == 0x28C);
// A new template with no references (operator new and Construct; app_lifetime.cpp).
// The application's start registers it with the class registry as class 1.
ClassTemplate* __cdecl CreateClassTemplate();
static_assert(offsetof(ClassTemplate, weights) == 0x10);
static_assert(offsetof(ClassTemplate, weightsTail) == 0x34);
static_assert(offsetof(ClassTemplate, flag38) == 0x38);
static_assert(offsetof(ClassTemplate, allowed) == 0x11C);
static_assert(offsetof(ClassTemplate, magic) == 0x1D4);
static_assert(sizeof(ItemStock) == 0xB8);

// STLport vector of counted pointers (start, finish, end of storage).
struct TemplateTable : stlp::vector<ClassTemplate*> {
    // Releases the templates, then builds new ones: four (classes 1, 3, 4,
    // 5) when `fourClasses` (low byte) is set, else five (1 to 5, the
    // fifth with zero weights).
    void Build(unsigned fourClasses);
    // The first template of class `type`, null if none.
    ClassTemplate* Find(int type) const;
};
using ClassTemplateVector = TemplateTable;
static_assert(sizeof(ClassTemplateVector) == 12);

// Adds to the template's lists every weapon, armor, quick item, fourth-kind
// record, spell and modifier whose class mask has the template's bit (spells and modifiers to
// both lists).
void __fastcall ClassTemplateFill(ClassTemplate* t);

// Run on the party manager (the script's quest completion): sets byte
// +0x38 of every template (+0x38 vector). Leaves the template count in EAX.
unsigned __fastcall ClassTemplatesQuestCompleted(PartyManager* manager);

// class_template_stock.cpp: the trader stock of a camp's constructor record.
// With flag +0x0C: the items party (+0x3C) is emptied and refilled from the
// "allowed" stock (its class index type - 1; the "magic" stock's spells and modifiers, fresh
// material units): 40-50 weapons, armors and quick items, 1-3 instructions of each, 20-50
// materials, each added (count kept, merged) and released.
void __fastcall ClassTemplateGenerateItems(ClassTemplate* t);
// With flag +0x0D: the spells party (+0xAC) is emptied and refilled from the
// "magic" stock: 40-50 scrolls, 1-3 spell instructions, 20-50 modifier instructions.
void __fastcall ClassTemplateGenerateMagic(ClassTemplate* t);
