#pragma once

// Compiled tags (C19; docs/notes/standalone.md, "Compiler-made vtables"): a class whose objects keep game.exe's
// layout with the vtable as a plain field (`void* vtable`) gets the compiler's table in place of game.exe's through
// a tag, a C++ class declaring the same slots whose bodies call the class's functions. Its constructors and
// destructors store `Tag::Vtable()`; the original's dispatch through the field reaches the tag's slots. The tag is
// never an object of its own: only its table is used (mfc_slots.h's CompiledCollection is the collections' tag).

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
};

template <typename F>
struct DeleteTarget;
template <typename R, typename C, typename Flags>
struct DeleteTarget<R (C::*)(Flags)> {
    using Type = C;
};

// A one-slot table whose slot 0 is the class's scalar deleting destructor (thiscall, the flags, RET 4: the
// destructor's body, the memory freed when bit 0 is set; returns the object): `Delete` is that member.
template <typename Self, auto Delete>
class CompiledDeleter : public CompiledTag<Self> {
public:
    virtual void* DeleteScalar(unsigned flags) {   // 0
        using Target = typename DeleteTarget<decltype(Delete)>::Type;
        return (reinterpret_cast<Target*>(this)->*Delete)(flags);
    }
};

} // namespace ei
