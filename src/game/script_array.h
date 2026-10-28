#pragma once

// CArray<T>::SetSize(n, -1) as the script code inlines it (loaders, variable appends), with the
// element helpers of each instantiation (ConstructElements / DestructElements / element
// destructors, called through their addresses). Defined in script_load.cpp.

#include "script.h"

#include "lib/crt.h"
#include "lib/mfc_array.h"

#include <cstring>
#include <type_traits>

namespace scriptarray {

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
    RangeFn construct;
    RangeFn destroyAll;    // n == 0 (null: plain delete)
    RangeFn destroyTail;   // shrinking (null: nothing)
};

// float (zeroes), ScriptObjectRef (each released), ScriptGroup (each destroyed),
// SSCCmd (each destroyed), ScriptIfThen (nothing to destroy), pointers (zeroes,
// nothing to destroy).
extern const ElementKind kFloats;
extern const ElementKind kRefs;
extern const ElementKind kGroups;
extern const ElementKind kCmds;
extern const ElementKind kIfThen;
extern const ElementKind kPointers;

template <typename T>
void InlineSetSize(mfc::CArray<T>* a, int n, const ElementKind& kind) {
    if (n == 0) {
        if (a->data) {
            if (kind.destroyAll) kind.destroyAll(a->data, a->size);
            gamecrt::operator_delete(a->data);
            a->data = nullptr;
        }
        a->maxSize = 0;
        a->size = 0;
        return;
    }
    if (!a->data) {
        a->data = gamecrt::operator_new_array<T>(static_cast<unsigned>(n));
        kind.construct(a->data, n);
        a->maxSize = n;
        a->size = n;
        return;
    }
    if (n <= a->maxSize) {
        if (a->size < n) {
            kind.construct(a->data + a->size, n - a->size);
        } else if (n < a->size && kind.destroyTail) {
            kind.destroyTail(a->data + n, a->size - n);
        }
        a->size = n;
        return;
    }
    int grow = a->growBy;
    if (grow == 0) {
        grow = a->size / 8;
        if (grow < 4) grow = 4;
        else if (grow > 0x400) grow = 0x400;
    }
    int newMax = a->maxSize + grow;
    if (n >= newMax) newMax = n;
    T* fresh = gamecrt::operator_new_array<T>(static_cast<unsigned>(newMax));
    std::memcpy(static_cast<void*>(fresh), a->data, static_cast<unsigned>(a->size) * sizeof(T));
    kind.construct(fresh + a->size, n - a->size);
    gamecrt::operator_delete(a->data);
    a->data = fresh;
    a->size = n;
    a->maxSize = newMax;
}

} // namespace scriptarray
