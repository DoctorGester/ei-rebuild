#pragma once

// The harness's routes of game.exe's import slots, named by their ImportTable member (lib/import_table.h): the slot
// through the data seam, in game.exe its own table, in the image ei_hook.dll's (S18f). tools/gen_iat.py's routed()
// reads the members named here (`ImportTable::X`): our code calls those imports through the slot (lib/gameapi.h),
// so the route applies in the image too. A route of game.exe's runs only (PatchOriginalImport, TraceOriginalSlot:
// a function with "Original" in its name taking `&ImportTable::X`) does not count: gen_iat.py leaves its slot out.

#include <windows.h>

#include "ei/hook.h"
#include "lib/import_table.h"

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

// PatchImport in game.exe's runs only, for an import only the original calls through its slot: our code calls it
// as the Windows function, or by a seam of ours that the harness takes instead, or not at all. In the image the
// slot is a trap no call reaches (tools/gen_iat.py's reached() leaves the slots named here out; its --check fails
// when our code reaches one), so there is nothing to route there.
template <class F, class T>
void PatchOriginalImport(F ImportTable::* member, T replacement, T& original) {
    if (!Standalone()) PatchImport(member, replacement, original);
}

} // namespace ei::verify
