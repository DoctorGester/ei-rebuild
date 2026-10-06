#pragma once

// Deep comparisons of the script engine's structures for the verify wrappers (src/game/script.h).

#include "../../src/game/script.h"

#include <cstring>

namespace scriptcompare {

inline bool SameText(const mfc::CString& a, const mfc::CString& b) {
    return std::strcmp(static_cast<const char*>(a), static_cast<const char*>(b)) == 0;
}

// Size, growth step and (when `capacity`) the allocated size.
template <typename T>
bool SameShape(const mfc::CArray<T>& a, const mfc::CArray<T>& b, bool capacity) {
    return a.vtable == b.vtable && a.size == b.size && a.growBy == b.growBy && (!capacity || a.maxSize == b.maxSize);
}

// Arrays of plain elements: shape and element bytes.
template <typename T>
bool SamePlain(const mfc::CArray<T>& a, const mfc::CArray<T>& b, bool capacity) {
    return SameShape(a, b, capacity) && (!a.size || std::memcmp(a.data, b.data, a.size * sizeof(T)) == 0);
}

inline bool SameStrings(const mfc::CArray<mfc::CString>& a, const mfc::CArray<mfc::CString>& b, bool capacity) {
    if (!SameShape(a, b, capacity)) return false;
    for (int i = 0; i < a.size; ++i)
        if (!SameText(a.data[i], b.data[i])) return false;
    return true;
}

// Two call trees: arguments, bindings and nested calls (their commands compared in turn).
inline bool SameCmd(const SSCCmd& a, const SSCCmd& b, bool capacity) {
    if (a.id != b.id || a.calledScript != b.calledScript) return false;
    if (!SamePlain(a.floatArgs, b.floatArgs, capacity) || !SameStrings(a.stringArgs, b.stringArgs, capacity) ||
        !SamePlain(a.objectArgs, b.objectArgs, capacity) || !SamePlain(a.groupArgs, b.groupArgs, capacity) ||
        !SamePlain(a.localBindings, b.localBindings, capacity) ||
        !SamePlain(a.globalBindings, b.globalBindings, capacity) || !SameShape(a.subCalls, b.subCalls, capacity))
        return false;
    for (int i = 0; i < a.subCalls.size; ++i) {
        const ScriptSubCall& x = a.subCalls.data[i];
        const ScriptSubCall& y = b.subCalls.data[i];
        if (x.type != y.type || x.argIndex != y.argIndex || !x.cmd != !y.cmd) return false;
        if (x.cmd && !SameCmd(*x.cmd, *y.cmd, capacity)) return false;
    }
    return true;
}

inline bool SameGroups(const mfc::CArray<ScriptGroup>& a, const mfc::CArray<ScriptGroup>& b, bool capacity) {
    if (!SameShape(a, b, capacity)) return false;
    for (int i = 0; i < a.size; ++i)
        if (!SamePlain<ScriptObjectRef>(a.data[i], b.data[i], false)) return false;
    return true;
}

// Every variable's name and value (floats by bits, groups member by member).
inline bool SameVars(const ScriptVars& a, const ScriptVars& b, bool capacity) {
    return SamePlain(a.floats, b.floats, capacity) && SameStrings(a.floatNames, b.floatNames, capacity) &&
           SameStrings(a.strings, b.strings, capacity) && SameStrings(a.stringNames, b.stringNames, capacity) &&
           SamePlain(a.objects, b.objects, capacity) && SameStrings(a.objectNames, b.objectNames, capacity) &&
           SameGroups(a.groups, b.groups, capacity) && SameStrings(a.groupNames, b.groupNames, capacity);
}

inline bool SameDefinition(const ScriptDefinition& a, const ScriptDefinition& b, bool capacity) {
    return a.index == b.index && SameText(a.name, b.name) && SameText(a.params, b.params) &&
           SamePlain(a.ifThen, b.ifThen, capacity) && SameVars(a.vars, b.vars, capacity);
}

} // namespace scriptcompare
