#pragma once

// MFC 4.2's non-template arrays (afxcoll: array_s.cpp, array_w.cpp, array_d.cpp, array_b.cpp),
// written out for the standalone exe (docs/notes/standalone.md section 2). Same layout and allocator as the
// copies linked into game.exe (mfc_coll.cpp).

#include "mfc_string.h"
#include "game/game_data.h"

#include <cstdint>

namespace mfc::impl {

// CWordArray / CDWordArray / CByteArray (0x14 bytes): vtable, data, size, allocated
// size, growth step (0: size / 8 clamped to 4..1024). The three are one source with another element
// type; new elements are zeroed, nothing is destroyed. `Class` names the instantiation's vtable.
template <typename T, typename Class>
struct PlainArray {
    void* vtable;
    T* data;        // operator new storage
    int size;
    int maxSize;
    int growBy;

    PlainArray* Construct();                        // the constructor (returns this)
    void Destruct();                                // the destructor
    PlainArray* DeletingDestructor(unsigned flags); // vtable slot 1
    void SetSize(int newSize, int newGrowBy);
    void SetAtGrow(int index, T value);
    void Copy(const PlainArray& source);            // SetSize to the source's, the elements copied
    void Serialize(CArchive* ar);                   // vtable slot 2
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
using CWordArray = PlainArray<std::uint16_t, WordArrayClass>;
using CDWordArray = PlainArray<std::uint32_t, DWordArrayClass>;
using CByteArray = PlainArray<std::uint8_t, ByteArrayClass>;
static_assert(sizeof(CWordArray) == 0x14);

// CStringArray (0x14 bytes): the same layout over CStrings; new elements are empty strings, removed
// ones are released.
struct CStringArray {
    void* vtable;
    CString* data;
    int size;
    int maxSize;
    int growBy;

    static void* Vtable();   // compiled (mfc_coll.cpp) in place

    CStringArray* Construct();
    void Destruct();
    CStringArray* DeletingDestructor(unsigned flags);   // (vtable slot 1)
    void SetSize(int newSize, int newGrowBy);
    void Serialize(CArchive* ar);                       // (vtable slot 2)

    // array_s.cpp's element helpers (cdecl).
    static void ConstructElements(CString* elements, int count);
    static void DestructElements(CString* elements, int count);
};
static_assert(sizeof(CStringArray) == 0x14);

} // namespace mfc::impl
