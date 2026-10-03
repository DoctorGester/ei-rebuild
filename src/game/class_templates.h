#pragma once

// Class templates (0x28C bytes, vtable 0073D0C4): one per hero class a new game offers, each with
// the weapons, armors, items, spells and modifiers that class may use (the records' class masks).
// Built by 004A7630 at new game into the vector at +0x38 of the object at 007AA5A0. Names are
// provisional.

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
    std::uint8_t items[0x70];       // +0x3C object whose slot 18 gives the item database
    std::uint8_t spells[0x70];      // +0xAC object whose slot 22 gives the spell database
    std::uint8_t allowed[0xB8];     // +0x11C everything the class may use (vectors at +0x08 armors,
                                    //   +0x14 weapons, +0x20 quick items, +0x2C the fourth kind
                                    //   (16-byte entries), +0x38 spells, +0x44 modifiers)
    std::uint8_t magic[0x28C - 0x1D4];   // +0x1D4 the same layout: spells and modifiers only
};
static_assert(sizeof(ClassTemplate) == 0x28C);
static_assert(offsetof(ClassTemplate, weights) == 0x10);
static_assert(offsetof(ClassTemplate, allowed) == 0x11C);
static_assert(offsetof(ClassTemplate, magic) == 0x1D4);

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
