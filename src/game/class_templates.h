#pragma once

// Class templates (a counted object): one per hero class a new game offers, each with the weapons, armors,
// items, spells and modifiers that class may use (the records' class masks). Built at new game into
// PartyManager::templates.

#include "core_refcount.h"
#include "item_stock.h"
#include "party.h"

struct PartyManager;

#include <cstddef>
#include <cstdint>
#include <vector>

class ClassTemplate : public CRefCounted {
public:
    // Both parties made (their managers too) and each stock on its party; no class, nothing sold, restock due.
    // weightsTail is zeroed too (the game never sets it, and saves it).
    ClassTemplate();
    ~ClassTemplate() override;
    // Fields 1 class (4 bytes), 2 / 3 the flags (1 byte each), 4 the weights and weightsTail, 5 (both parties
    // without variables, then both stocks' lists: SaveLists / LoadLists), 6 restockDue (1 byte).
    void Serialize(ChunkArchive* ar) override;
    // Both stocks take the 8 base and 8 scroll levels.
    void SetLevels(const float* base, const float* scroll);

    CParty* ItemsParty() { return &items; }
    CParty* SpellsParty() { return &spells; }

    int type = -1;                  // class 1..5; bit (type - 1) of the records' class masks
    std::uint8_t sellsItems = 0;    // the constructor's trader sells items (ClassTemplateGenerateItems)
    std::uint8_t sellsSpells = 0;   // and spells (ClassTemplateGenerateMagic)
    float weights[9];
    std::uint8_t weightsTail[4] = {};   // field 4's tenth dword (archived with weights)
    std::uint8_t restockDue = 1;    // set by the constructor and by a quest completion; the camp restocks and clears it
    // the party objects (ItemsParty / SpellsParty).
    CParty items;
    CParty spells;
    ItemStock allowed{&items};      // everything the class may use
    ItemStock magic{&spells};       // the same layout: spells and modifiers only
};
// A new template with no references; the application's start registers it with the class registry as class 1.
CRefCounted* CreateClassTemplate();

// Counted pointers.
struct TemplateTable : std::vector<HeldRef<ClassTemplate>> {
    // Releases the templates, then builds new ones: four (classes 1, 3, 4, 5) when `fourClasses` (low byte) is
    // set, else five (1 to 5, the fifth with zero weights).
    void Build(unsigned fourClasses);
    ClassTemplate* Find(int type) const;
};

// Adds to the template's lists every weapon, armor, quick item, fourth-kind record, spell and modifier whose
// class mask has the template's bit (spells and modifiers to both lists).
void ClassTemplateFill(ClassTemplate* t);

// The script's quest completion: sets restockDue of every template. Returns the template count.
unsigned ClassTemplatesQuestCompleted(PartyManager* manager);

// The trader stock of a camp's constructor record. With sellsItems: the items party is emptied and refilled from
// the "allowed" stock (its class index type - 1; the "magic" stock's spells and modifiers, fresh material
// units): 40-50 weapons, armors and quick items, 1-3 instructions of each, 20-50 materials, each added (count
// kept, merged) and released.
void ClassTemplateGenerateItems(ClassTemplate* t);
// With sellsSpells: the spells party is emptied and refilled from the "magic" stock: 40-50 scrolls, 1-3 spell
// instructions, 20-50 modifier instructions.
void ClassTemplateGenerateMagic(ClassTemplate* t);
