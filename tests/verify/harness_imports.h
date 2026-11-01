#pragma once

// The harness's routes of game.exe's import slots, named by their ImportTable member
// (platform/win32/import_table.h): the program's own table. tools/gen_iat.py's routed() reads the members named here
// (`ImportTable::X`): our code calls those imports through the slot (platform/win32/gameapi.h), so the route applies.

#include <windows.h>

#include "platform/win32/import_table.h"

namespace ei::verify {

// The slot of `member` as the harness's type of its function (the same size as the member's).
template <class T, class F>
T& ImportSlot(F ImportTable::* member) {
    static_assert(sizeof(T) == sizeof(F));
    return reinterpret_cast<T&>(Imports().*member);
}

// Puts `replacement` in the slot of `member`, keeping what it held in `original`.
template <class F, class T>
void PatchImport(F ImportTable::* member, T replacement, T& original) {
    T& slot = ImportSlot<T>(member);
    DWORD old;
    VirtualProtect(&slot, sizeof(slot), PAGE_READWRITE, &old);
    original = slot;
    slot = replacement;
    VirtualProtect(&slot, sizeof(slot), old, &old);
}

} // namespace ei::verify
