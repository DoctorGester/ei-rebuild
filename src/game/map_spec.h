#pragma once

// The zone file's object records (the .mob objects; 00469630-0046F160): chunk readers / writers
// (Chunk, chunk_stream.h) that are also the map editor's live objects. Every record type has a
// 12-slot vtable:
//   0 OnChunk (reads the current chunk's value by its id), 1 File (Chunk), 2 the world object by
//   id, 3 (empty), 4 the type name (CString by value), 5 Move(dx, dy, dz), 6 remove the world
//   object, 7 make or update the world object, 8 refresh it, 9 Save (a chunk of its own), 10 the
//   deleting destructor, 11 WriteFields (the common fields, also called directly by the
//   subclasses' Save).
// The record classes (vtables, kind at +0x48): the base 0073C5C4 (0); objects 0073C5F0 (1); flames
// 0073C620 (6); magic traps 0073C650 (7); levers 0073C6B0 (8); units 0073C6E0 (5). Names are
// provisional; see docs/notes/objects.md ("Sixth round").
#include "chunk_stream.h"
#include "maths.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

namespace mapspec {

// An MFC CStringArray (0x14 bytes; constructor 006FE925, destructor 006FE958).
struct StringArray {
    void* vtable;
    mfc::CString* data;
    int size;
    int max;
    int grow;
};
static_assert(sizeof(StringArray) == 0x14);

// An MFC CArray of plain elements (0x14 bytes).
struct PlainArray {
    void* vtable;
    std::uint8_t* data;
    int size;
    int max;
    int grow;
};
static_assert(sizeof(PlainArray) == 0x14);

// The base record (vtable 0073C5C4, 0x4C bytes).
struct Spec {
    Chunk chunk;                 // +0x00
    std::int32_t id;             // +0x18 the object's id (-1)
    void* object;                // +0x1C the world object made from the record (null: none)
    Vector3 position;            // +0x20
    Quaternion rotation;         // +0x2C (1, 0, 0, 0)
    mfc::CString name;           // +0x3C
    mfc::CString comment;        // +0x40
    std::int32_t unknown_44;     // +0x44 (-1)
    std::int32_t kind;           // +0x48
};
static_assert(offsetof(Spec, position) == 0x20);
static_assert(offsetof(Spec, name) == 0x3C);
static_assert(sizeof(Spec) == 0x4C);

// An object record (vtable 0073C5F0, 0x8C bytes).
struct ObjectSpec {
    Spec base;                   // +0x00
    mfc::CString model;          // +0x4C (from 0079B298, empty)
    std::int32_t parentId;       // +0x50
    mfc::CString texture;        // +0x54
    mfc::CString primaryTexture; // +0x58
    std::int32_t player;         // +0x5C (-1); the type name formats it
    float scale[3];              // +0x60 (0.5 each; read and written y, x, z)
    std::uint8_t shadow;         // +0x6C
    std::uint8_t visible;        // +0x6D (1)
    std::uint8_t unknown_6E;     // +0x6E
    std::uint8_t unknown_6F;
    mfc::CString parentIname;    // +0x70
    mfc::CString unknown_74;     // +0x74
    StringArray parts;           // +0x78
};
static_assert(offsetof(ObjectSpec, model) == 0x4C);
static_assert(offsetof(ObjectSpec, scale) == 0x60);
static_assert(offsetof(ObjectSpec, parts) == 0x78);
static_assert(sizeof(ObjectSpec) == 0x8C);

// A flame (vtable 0073C620, 0xA0 bytes).
struct FlameSpec {
    ObjectSpec object;           // +0x00
    float volume;                // +0x8C (0.7)
    Vector3 offset;              // +0x90
    mfc::CString sound;          // +0x9C ("nature\fire\small.wav")
};
static_assert(sizeof(FlameSpec) == 0xA0);

// A magic trap (vtable 0073C650, 0xD0 bytes).
struct TrapSpec {
    ObjectSpec object;           // +0x00
    std::int32_t diplomacy;      // +0x8C (-1)
    mfc::CString spell;          // +0x90
    std::uint8_t unknown_94[0xC];
    PlainArray areas;            // +0xA0 12-byte elements (vtable 0073C68C)
    PlainArray targets;          // +0xB4 8-byte elements (vtable 0073C680)
    std::int32_t interval;       // +0xC8 (15)
    std::uint8_t castOnce;       // +0xCC
    std::uint8_t unknown_CD[3];
};
static_assert(offsetof(TrapSpec, areas) == 0xA0);
static_assert(offsetof(TrapSpec, interval) == 0xC8);
static_assert(sizeof(TrapSpec) == 0xD0);

// A lever (vtable 0073C6B0, 0xA4 bytes).
struct LeverSpec {
    ObjectSpec object;           // +0x00
    std::uint32_t stats[3];      // +0x8C (1, 0, 0): a 12-byte blob
    std::uint32_t state;         // +0x98
    std::uint32_t states;        // +0x9C (2)
    std::uint8_t cycled;         // +0xA0
    std::uint8_t castOnce;       // +0xA1
    std::uint8_t unknown_A2;     // +0xA2
    std::uint8_t unknown_A3;
};
static_assert(sizeof(LeverSpec) == 0xA4);

} // namespace mapspec
