#pragma once

// Constructors and destructors of the engine's stream and reference-counted base classes, and two
// small allocation helpers (00410000-00430000, 00470000-00480000). See docs/notes/unmapped_410_470.md.
// Names are provisional.
//
// Class map (vtables):
//   0073BB68  CRefObject root: slot 0 deleting destructor, slot 1 empty (RET 4). Its destructor
//             body only restores this vtable (two identical copies, 00412720 and 00413430).
//   0073BB58  abstract reference-counted value (adds pure slots 2 and 3, see core_refcount.h);
//             constructor body 00419720, deleting destructor 00412700.
//   0073BDB0  CStream: abstract byte stream (slots 1-3 pure). +5 ok flag, +8 a limit (2000000000).
//   0073BD90  CMemoryStream (object.h): slot 1 Read 004A5380, 2 Write 004A53F0, 3 Seek 004A5490,
//             4 Size (+0x24), 5 Position (+0x20), 6 Clear 00417350, 7 004A55D0. 0073C960 is a
//             second vtable with the same slots 1-7 and another copy of the destructor (004790D0).

#include "net_coder.h"
#include "object.h"

#include <cstdint>

constexpr std::uintptr_t kRefObjectRootVtable = 0x0073BB68;
constexpr std::uintptr_t kRefValueVtable = 0x0073BB58;
constexpr std::uintptr_t kStreamVtable = 0x0073BDB0;
constexpr std::uintptr_t kMemoryStreamVtable = 0x0073BD90;

// CRefObject root (vtable 0073BB68).
void __fastcall RefObjectDestructBase(void* self);                                 // 00412720, 00413430
void* __fastcall RefObjectDeletingDestructor(void* self, void* edx, unsigned flags); // 00412730 (slot 0)
// Abstract reference-counted value (vtable 0073BB58).
void* __fastcall RefValueConstructBase(void* self);                                // 00419720, returns this
void* __fastcall RefValueDeletingDestructor(void* self, void* edx, unsigned flags);  // 00412700 (slot 0)

// CStream (vtable 0073BDB0).
void* __fastcall StreamConstructBase(void* self);                                  // 0047A200, returns this
void __fastcall StreamDestructBase(void* self);                                    // 004174E0
void* __fastcall StreamDeletingDestructor(void* self, void* edx, unsigned flags);    // 00417400 (slot 0)

// CMemoryStream (vtable 0073BD90).
CMemoryStream* __fastcall MemoryStreamConstruct(CMemoryStream* self);              // 0041DD70, returns this
void __fastcall MemoryStreamDestruct(CMemoryStream* self);                         // 004173D0, 004790A0
void* __fastcall MemoryStreamDeletingDestructor(CMemoryStream* self, void* edx, unsigned flags); // 004173B0, 004790D0
void __fastcall MemoryStreamClear(CMemoryStream* self);                            // 00417350 (slot 6)

// 0047B940. Allocates room for `count` pointers on a 32-byte boundary; the block returned by
// operator new is stored just before the aligned pointer (freed through it by 0047B620).
void** __fastcall AlignedPointerArrayAlloc(int count);

// An array of object pointers in an aligned block (0047B940): data, size, capacity.
struct AlignedObjectArray {
    void** data;
    int size;
    int capacity;

    // 0047B620. Calls slot 4 of every element, frees the block and empties the array.
    void Clear();
};
static_assert(sizeof(AlignedObjectArray) == 12);

// The 0x2828-byte coder state (CoderState) is declared in net_coder.h; its constructor 0042FCA0
// is in core_streams.cpp.
