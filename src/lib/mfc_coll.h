#pragma once

// MFC 4.2's non-template arrays (afxcoll: array_s.cpp, array_w.cpp, array_d.cpp, array_b.cpp,
// array_u.cpp), written out for the standalone exe (ticket S5, docs/notes/standalone.md section 2).
// Same layout and allocator as the copies linked into game.exe; every body is registered at its
// library address (mfc_coll.cpp) and checked in game by verify_mfc_coll.cpp until S21d. The callers keep
// going through the forwarders (mfc.h's CStringArray, src/game/originals_library.h) until C13.

#include "mfc_string.h"

#include <cstdint>

namespace mfc::impl {

// CWordArray / CDWordArray / CByteArray / CUIntArray (0x14 bytes): vtable, data, size, allocated
// size, growth step (0: size / 8 clamped to 4..1024). The four are one source with another element
// type; new elements are zeroed, nothing is destroyed. `Class` names the instantiation's vtable.
template <typename T, typename Class>
struct PlainArray {
    void* vtable;
    T* data;        // +0x04 operator new storage
    int size;       // +0x08
    int maxSize;    // +0x0C
    int growBy;     // +0x10

    PlainArray* Construct();                        // the constructor (returns this)
    void Destruct();                                // the destructor
    PlainArray* DeletingDestructor(unsigned flags); // vtable slot 1
    void SetSize(int newSize, int newGrowBy);
    void SetAtGrow(int index, T value);
    void Serialize(CArchive* ar);                   // vtable slot 2 (not CUIntArray's)
};

// The instantiations' vtables (game data).
struct WordArrayClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x00745F78); }
};
struct DWordArrayClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x007464B8); }
};
struct ByteArrayClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x00746560); }
};
struct UIntArrayClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x007467A0); }
};
using CWordArray = PlainArray<std::uint16_t, WordArrayClass>;
using CDWordArray = PlainArray<std::uint32_t, DWordArrayClass>;
using CByteArray = PlainArray<std::uint8_t, ByteArrayClass>;
using CUIntArray = PlainArray<unsigned, UIntArrayClass>;
static_assert(sizeof(CWordArray) == 0x14);

// CStringArray (0x14 bytes): the same layout over CStrings; new elements are empty strings, removed
// ones are released.
struct CStringArray {
    void* vtable;
    CString* data;  // +0x04
    int size;       // +0x08
    int maxSize;    // +0x0C
    int growBy;     // +0x10

    static void* Vtable() { return reinterpret_cast<void*>(0x00745F5C); }

    CStringArray* Construct();                          // 006FE925
    void Destruct();                                    // 006FE958
    CStringArray* DeletingDestructor(unsigned flags);   // 006FE93C (vtable slot 1)
    void SetSize(int newSize, int newGrowBy);           // 006FE9B8
    int Append(const CStringArray& source);             // 006FEB01 (returns the old size)
    void Copy(const CStringArray& source);              // 006FEB61
    void SetAtGrow(int index, const char* text);        // 006FEBCE
    void SetAtGrow(int index, const CString& value);    // 006FEBFA
    void InsertEmpty(int index, int count);             // 006FEC26
    void InsertAt(int index, const CString& value, int count);   // 006FED03
    void RemoveAt(int index, int count);                // 006FED3C
    void Serialize(CArchive* ar);                       // 006FEE31 (vtable slot 2)

    // array_s.cpp's element helpers (cdecl).
    static void __cdecl ConstructElements(CString* elements, int count);     // 006FEAD5
    static void __cdecl DestructElements(CString* elements, int count);      // 006FE996
    static void __cdecl CopyElements(CString* dest, const CString* source, int count);   // 006FEB37
};
static_assert(sizeof(CStringArray) == 0x14);

} // namespace mfc::impl
