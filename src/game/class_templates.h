#pragma once

// Class templates (0x28C bytes, vtable 0073D0C4): one per hero class a new game offers, each with
// the weapons, armors, items, spells and modifiers that class may use (the records' class masks).
// Built by 004A7630 at new game into the vector at +0x38 of the object at 007AA5A0. Names are
// provisional.

#include "item_stock.h"

#include <cstddef>
#include <cstdint>

struct ClassTemplate {
    void* vtable;                   // 0073D0C4 (base 0073BB68)
    std::int16_t refCount;          // +0x04 counted pointers (the dword is cleared before construction)
    std::int16_t unknown_06;
    int type;                       // +0x08 class 1..5; bit (type - 1) of the records' class masks
    std::uint8_t flag0C;            // +0x0C
    std::uint8_t flag0D;            // +0x0D
    std::uint8_t unknown_0E[2];
    std::uint32_t weights[9];       // +0x10..+0x30 floats (bit patterns)
    std::uint8_t unknown_34[0x3C - 0x34];
    std::uint8_t items[0x70];       // +0x3C party object (00661050); slot 18: its item manager (+0x1C)
    std::uint8_t spells[0x70];      // +0xAC party object; slot 22: its spell manager (+0x24)
    ItemStock allowed;              // +0x11C everything the class may use (vectors at +0x08 armors,
                                    //   +0x14 weapons, +0x20 quick items, +0x2C the fourth kind
                                    //   (16-byte entries), +0x38 spells, +0x44 modifiers)
    ItemStock magic;                // +0x1D4 the same layout: spells and modifiers only
};
static_assert(sizeof(ClassTemplate) == 0x28C);
static_assert(offsetof(ClassTemplate, weights) == 0x10);
static_assert(offsetof(ClassTemplate, allowed) == 0x11C);
static_assert(offsetof(ClassTemplate, magic) == 0x1D4);
static_assert(sizeof(ItemStock) == 0xB8);

// STLport vector of counted pointers (start, finish, end of storage).
struct ClassTemplateVector {
    ClassTemplate** start;
    ClassTemplate** finish;
    ClassTemplate** eos;
};

// 004A7080 (__thiscall). Adds to the template's lists every weapon, armor, quick item, fourth-kind
// record, spell and modifier whose class mask has the template's bit (spells and modifiers to
// both lists).
void __fastcall ClassTemplateFill(ClassTemplate* t);

// 004A7630 (__thiscall). Releases the vector's templates, then builds new ones: four (classes 1, 3,
// 4, 5) when `fourClasses` (low byte) is set (004A88C0, called from 0057F280), else five (1 to 5,
// the fifth with zero weights; 004A88A0).
void __fastcall ClassTemplatesBuild(ClassTemplateVector* v, void* edx, unsigned fourClasses);

// 004A74F0 (__thiscall). The first template of class `type`, null if none.
ClassTemplate* __fastcall ClassTemplatesFind(const ClassTemplateVector* v, void* edx, int type);
// 004A88E0 (__thiscall on the party manager 007AA5A0, the script's quest completion): sets byte
// +0x38 of every template (+0x38 vector). Leaves the template count in EAX.
unsigned __fastcall ClassTemplatesQuestCompleted(void* manager);

// class_template_stock.cpp (A-S4): the trader stock of a camp's constructor record.
// 004A6DC0 (__thiscall, RET 8). Both stocks take the 8 base and 8 scroll levels (ItemStock::SetLevels).
void __fastcall ClassTemplateSetLevels(ClassTemplate* t, void* edx, const float* base, const float* scroll);
// 004A6DF0 (__thiscall). With flag +0x0C: the items party (+0x3C) is emptied and refilled from the
// "allowed" stock (its class index type - 1; the "magic" stock's spells and modifiers, fresh
// material units): 40-50 weapons, armors and quick items, 1-3 instructions of each, 20-50
// materials, each added (count kept, merged) and released.
void __fastcall ClassTemplateGenerateItems(ClassTemplate* t);
// 004A6F60 (__thiscall). With flag +0x0D: the spells party (+0xAC) is emptied and refilled from the
// "magic" stock: 40-50 scrolls, 1-3 spell instructions, 20-50 modifier instructions.
void __fastcall ClassTemplateGenerateMagic(ClassTemplate* t);
