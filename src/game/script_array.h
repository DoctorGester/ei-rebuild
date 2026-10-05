#pragma once

// CArray<T>::SetSize(n, -1) as the script code inlines it (loaders, variable appends), with the
// element helpers of each instantiation (ConstructElements / DestructElements / element
// destructors, called through their addresses). Defined in script_load.cpp.

#include "script.h"

namespace scriptarray {

// Any CArray (0x14 bytes) seen as bytes.
struct RawArray {
    void* vtable;
    char* data;     // +0x04
    int size;       // +0x08
    int maxSize;    // +0x0C
    int growBy;     // +0x10
};
template <typename T>
RawArray* Raw(MfcArray<T>* a) { return reinterpret_cast<RawArray*>(a); }

using RangeFn = void(__stdcall*)(void* first, int count);

// The element helpers of one instantiation, as the inlined SetSize uses them.
struct ElementKind {
    int size;
    RangeFn construct;
    void (*destroyAll)(char* first, int count);    // n == 0 (null: plain delete)
    void (*destroyTail)(char* first, int count);   // shrinking (null: nothing)
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
