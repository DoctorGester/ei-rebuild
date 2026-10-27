#pragma once

// MFC 4.2's non-template arrays (afxcoll: array_s.cpp, array_w.cpp, array_d.cpp, array_b.cpp,
// array_u.cpp), written out for the standalone exe (docs/notes/standalone.md section 2).
// Same layout and allocator as the copies linked into game.exe; every body is registered at its
// library address (mfc_coll.cpp).

#include "mfc_string.h"
#include "game/game_data.h"

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
    void Copy(const PlainArray& source);            // SetSize to the source's, the elements copied
    void Serialize(CArchive* ar);                   // vtable slot 2 (not CUIntArray's)
};

// The instantiations' tables: compiled (mfc_coll.cpp) in place
struct WordArrayClass {
    static void* Vtable();
};
struct DWordArrayClass {
    static void* Vtable();
};
struct ByteArrayClass {
    static void* Vtable();
};
struct UIntArrayClass {
    static void* Vtable();
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

    static void* Vtable();   // compiled (mfc_coll.cpp) in place

    CStringArray* Construct();
    void Destruct();
    CStringArray* DeletingDestructor(unsigned flags);   // (vtable slot 1)
    void SetSize(int newSize, int newGrowBy);
    int Append(const CStringArray& source);             // (returns the old size)
    void Copy(const CStringArray& source);
    void SetAtGrow(int index, const char* text);
    void SetAtGrow(int index, const CString& value);
    void InsertEmpty(int index, int count);
    void InsertAt(int index, const CString& value, int count);
    void RemoveAt(int index, int count);
    void Serialize(CArchive* ar);                       // (vtable slot 2)

    // array_s.cpp's element helpers (cdecl).
    static void __cdecl ConstructElements(CString* elements, int count);
    static void __cdecl DestructElements(CString* elements, int count);
    static void __cdecl CopyElements(CString* dest, const CString* source, int count);
};
static_assert(sizeof(CStringArray) == 0x14);

} // namespace mfc::impl
