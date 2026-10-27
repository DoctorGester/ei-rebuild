#pragma once

// Compiled tags (docs/notes/standalone.md, "Compiler-made vtables"): a class whose objects keep game.exe's
// layout with the vtable as a plain field (`void* vtable`) gets the compiler's table in place of game.exe's through
// a tag, a C++ class declaring the same slots whose bodies call the class's functions. Its constructors and
// destructors store `Tag::Vtable()`; a dispatch through the field reaches the tag's slots. The tag is
// never an object of its own: only its table is used (mfc_slots.h's CompiledCollection is the collections' tag).

#include "ei/hook.h"

#include <intrin.h>

#include <cstdint>
#include <new>
#include <type_traits>

namespace ei {

// The table a T's constructor stores in its first dword (T built in a buffer and left: it holds nothing to destroy).
template <typename T>
void* CompiledVtable() {
    static_assert(std::is_polymorphic_v<T>);
    alignas(T) unsigned char storage[sizeof(T)];
    return *reinterpret_cast<void**>(::new (static_cast<void*>(storage)) T);
}

// A tag's accessor: `Self::Vtable()` is Self's compiled table.
template <typename Self>
struct CompiledTag {
    static void* Vtable() { return CompiledVtable<Self>(); }
    // The same table as its slots, for classes whose vtable field is a `void**`.
    static void** Slots() { return static_cast<void**>(CompiledVtable<Self>()); }

protected:
    // The object a slot runs on, as a T. The tag is never an object of its own: its slots are reached through the
    // table an object of the class stores, so their `this` is that object (the one place this view is taken).
    template <typename T>
    T* ObjectAs() { return reinterpret_cast<T*>(static_cast<Self*>(this)); }
    template <typename T>
    const T* ObjectAs() const { return reinterpret_cast<const T*>(static_cast<const Self*>(this)); }
};

template <typename F>
struct DeleteTarget;
template <typename R, typename C, typename Flags>
struct DeleteTarget<R (C::*)(Flags)> {
    using Type = C;
};

// A one-slot table whose slot 0 is the class's scalar deleting destructor (the flags: the
// destructor's body, the memory freed when bit 0 is set; returns the object): `Delete` is that member.
template <typename Self, auto Delete>
class CompiledDeleter : public CompiledTag<Self> {
public:
    virtual void* DeleteScalar(unsigned flags) {   // 0
        using Target = typename DeleteTarget<decltype(Delete)>::Type;
        return (this->template ObjectAs<Target>()->*Delete)(flags);
    }
};

// The body of a compiled table's slot whose game.exe body at `address` is not in our source: the image never reaches it
// (config/standalone_unreachable.csv lists the address with the reason: the class is never made, or never called
// through that slot). Reached anyway, it is reported as missing code at `address`.
[[noreturn]] __declspec(noinline) inline void UnportedSlot(std::uintptr_t address) {
    ReportNoCode(address, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}

} // namespace ei
