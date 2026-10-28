#pragma once

// MFC 4.2's non-template arrays (afxcoll: array_s.cpp, array_w.cpp, array_d.cpp, array_b.cpp),
// written out for the standalone exe (docs/notes/standalone.md section 2). Same layout and allocator as the
// copies linked into game.exe (mfc_coll.cpp).

#include "mfc.h"
#include "game/game_data.h"

#include <cstdint>

namespace mfc::impl {

struct CArchive;

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

} // namespace mfc::impl
