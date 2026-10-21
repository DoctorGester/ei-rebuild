#pragma once

// Class templates (0x28C bytes, vtable 0073D0C4): one per hero class a new game offers, each with
// the weapons, armors, items, spells and modifiers that class may use (the records' class masks).
// Built by 004A7630 at new game into the vector at +0x38 of the object at 007AA5A0. Names are
// provisional.

#include "item_stock.h"
#include "party_storage.h"
#include "lib/stlp.h"

class CParty;   // party.h
class ChunkArchive;

#include <cstddef>
#include <cstdint>

struct ClassTemplate {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073D0C4);
    static constexpr std::uintptr_t kBaseVtableAddress = std::uintptr_t(0x0073BB68);   // the counted root (CRefCounted)

    // 004A6580 (class_template_stock.cpp). Constructor body (+4 is left to the caller): the base
    // vtable, the two parties (CParty::Construct), the stocks (ItemStock::Construct on the items /
    // spells party; the first one inlined in the original), the vtable, both parties created (slot
    // 2), class -1, both flags 0, flag38 1. Returns this.
    ClassTemplate* Construct();
    // 004A6A90 (class_template_stock.cpp). Destructor body: the vtable, both parties destroyed (slot
    // 3), the stocks' maps and vectors freed (the magic stock first, each stock's context cleared),
    // both parties' destructor bodies (spells first), the base vtable.
    void Destruct();
    // 004A6A70 (slot 0, RET 4). Destruct, then operator delete when bit 0 of `flags` is set.
    ClassTemplate* DeletingDestruct(unsigned flags);
    // 004A6C40 (slot 1 Serialize, RET 4; party_manager.cpp). Fields 1 class (4 bytes), 2 / 3 the
    // flags (1 byte each), 4 the weights and the 4 bytes after them (0x28), stream field 5 (both
    // parties without variables, then both stocks' lists: SaveLists 0069BAC0 / LoadLists
    // 0069B5B0), field 6 flag38 (1 byte).
    void Serialize(ChunkArchive* ar);
    // 004A6DC0 (__thiscall, RET 8; class_template_stock.cpp). Both stocks take the 8 base and 8 scroll
    // levels (ItemStock::SetLevels).
    void SetLevels(const float* base, const float* scroll);

    CParty* ItemsParty() { return items.Party(); }
    CParty* SpellsParty() { return spells.Party(); }

    void* vtable;                   // 0073D0C4 (base 0073BB68)
    std::int16_t refCount;          // +0x04 counted pointers (the dword is cleared before construction)
    std::int16_t refsHigh;              // +0x06 the reference dword's high half (zeroed with it)
    int type;                       // +0x08 class 1..5; bit (type - 1) of the records' class masks
    std::uint8_t flag0C;            // +0x0C
    std::uint8_t flag0D;            // +0x0D
    std::uint8_t unused_0E[2];
    std::uint32_t weights[9];       // +0x10..+0x30 floats (bit patterns)
    std::uint8_t weightsTail[4];        // +0x34 field 4's tenth dword (archived with weights)
    std::uint8_t flag38;            // +0x38 set by the constructor and by a quest completion (004A88E0)
    std::uint8_t unused_39[3];
    // +0x3C / +0xAC the party objects (CParty, constructor 00661050; party_storage.h): ItemsParty /
    // SpellsParty.
    PartyStorage items;
    PartyStorage spells;
    ItemStock allowed;              // +0x11C everything the class may use (vectors at +0x08 armors,
                                    //   +0x14 weapons, +0x20 quick items, +0x2C the fourth kind
                                    //   (16-byte entries), +0x38 spells, +0x44 modifiers)
    ItemStock magic;                // +0x1D4 the same layout: spells and modifiers only
};
static_assert(sizeof(ClassTemplate) == 0x28C);
// 00457850 (A-E1; app_lifetime.cpp). A new template with no references (operator new and
// Construct). The application's start registers it with the class registry at 0079B270 as class 1.
ClassTemplate* __cdecl CreateClassTemplate();
static_assert(offsetof(ClassTemplate, weights) == 0x10);
static_assert(offsetof(ClassTemplate, weightsTail) == 0x34);
static_assert(offsetof(ClassTemplate, flag38) == 0x38);
static_assert(offsetof(ClassTemplate, allowed) == 0x11C);
static_assert(offsetof(ClassTemplate, magic) == 0x1D4);
static_assert(sizeof(ItemStock) == 0xB8);

// STLport vector of counted pointers (start, finish, end of storage).
struct TemplateTable : stlp::vector<ClassTemplate*> {
    // 004A7630 (__thiscall, RET 4). Releases the templates, then builds new ones: four (classes 1, 3, 4,
    // 5) when `fourClasses` (low byte) is set (004A88C0, called from 0057F280), else five (1 to 5, the
    // fifth with zero weights; 004A88A0).
    void Build(unsigned fourClasses);
    // 004A74F0 (__thiscall, RET 4). The first template of class `type`, null if none.
    ClassTemplate* Find(int type) const;
};
using ClassTemplateVector = TemplateTable;
static_assert(sizeof(ClassTemplateVector) == 12);

// 004A7080 (__thiscall). Adds to the template's lists every weapon, armor, quick item, fourth-kind
// record, spell and modifier whose class mask has the template's bit (spells and modifiers to
// both lists).
void __fastcall ClassTemplateFill(ClassTemplate* t);

// 004A88E0 (__thiscall on the party manager 007AA5A0, the script's quest completion): sets byte
// +0x38 of every template (+0x38 vector). Leaves the template count in EAX.
unsigned __fastcall ClassTemplatesQuestCompleted(void* manager);

// class_template_stock.cpp (A-S4): the trader stock of a camp's constructor record.
// 004A6DF0 (__thiscall). With flag +0x0C: the items party (+0x3C) is emptied and refilled from the
// "allowed" stock (its class index type - 1; the "magic" stock's spells and modifiers, fresh
// material units): 40-50 weapons, armors and quick items, 1-3 instructions of each, 20-50
// materials, each added (count kept, merged) and released.
void __fastcall ClassTemplateGenerateItems(ClassTemplate* t);
// 004A6F60 (__thiscall). With flag +0x0D: the spells party (+0xAC) is emptied and refilled from the
// "magic" stock: 40-50 scrolls, 1-3 spell instructions, 20-50 modifier instructions.
void __fastcall ClassTemplateGenerateMagic(ClassTemplate* t);
