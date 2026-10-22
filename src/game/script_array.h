#pragma once

// CArray<T>::SetSize(n, -1) as the script code inlines it (loaders, variable appends), with the
// element helpers of each instantiation (ConstructElements / DestructElements / element
// destructors, called through their addresses). Defined in script_load.cpp.

#include "script.h"

#include "lib/mfc_array.h"

#include <type_traits>

namespace scriptarray {

// Any CArray (0x14 bytes) seen as bytes; the element size comes from its ElementKind.
using RawArray = mfc::CArray<char>;
template <typename T>
RawArray* Raw(mfc::CArray<T>* a) { return reinterpret_cast<RawArray*>(a); }

using RangeFn = void(__stdcall*)(void* first, int count);

// An instantiation's ConstructElements as a RangeFn (the library's ArrayAbi member, called directly).
template <typename Abi>
void __stdcall ConstructAs(void* first, int count) {
    Abi::ConstructElements(static_cast<typename Abi::Element*>(first), count);
}

// An instantiation's DestructElements as a RangeFn.
template <typename Abi>
void __stdcall DestructAs(void* first, int count) {
    Abi::DestructElements(static_cast<typename Abi::Element*>(first), count);
}

// Destroys `count` elements T from `first` one by one with `Destroy`: a member taking the flags
// (called with 0, destroy in place) or a function of T* (an element destructor of the game's).
template <typename T, auto Destroy>
void __stdcall DestroyEach(void* first, int count) {
    T* element = static_cast<T*>(first);
    for (; count > 0; --count, ++element) {
        if constexpr (std::is_member_function_pointer_v<decltype(Destroy)>) (element->*Destroy)(0);
        else Destroy(element);
    }
}

// The element helpers of one instantiation, as the inlined SetSize uses them.
struct ElementKind {
    int size;
    RangeFn construct;
    RangeFn destroyAll;    // n == 0 (null: plain delete)
    RangeFn destroyTail;   // shrinking (null: nothing)
};

// float (004CC870 zeroes), CString (006FE268 / 006FE296), ScriptObjectRef (004CC800 /
// 004CE1E0 each / 004CC7D0), ScriptGroup (004CC9B0 / 004CE210 each / 004CC980), SSCCmd (004CE920
// / 004BF620 each), ScriptIfThen (004CD4A0, nothing to destroy), pointers (004CCA10 zeroes,
// nothing to destroy).
extern const ElementKind kFloats;
extern const ElementKind kStrings;
extern const ElementKind kRefs;
extern const ElementKind kGroups;
extern const ElementKind kCmds;
extern const ElementKind kIfThen;
extern const ElementKind kPointers;

void InlineSetSize(RawArray* a, int n, const ElementKind& kind);

} // namespace scriptarray
