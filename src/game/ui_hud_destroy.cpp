// Destructors of the battle HUD's panels,
// with the deleting destructors and the input sides' copies. See ui.h
// and docs/notes/ui.md.
#include "ui.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "game_data.h"
#include "lib/mfc_slots.h"

namespace {
// The vtables the inlined MFC array destructors write before freeing the storage.
void* QuickEntriesVtable() { return mfc::impl::QuickEntryArrayClass::Vtable(); }   // CArray<Entry>
void* DwordArrayVtable() { return mfc::impl::IntArrayClass::Vtable(); }     // CDWordArray
} // namespace

CHudWeaponBar::~CHudWeaponBar() = default;

CHudQuickItems::~CHudQuickItems() {
    entries_.vtable = QuickEntriesVtable();
    if (entries_.data) gamecrt::operator_delete(entries_.data);
}

CHudSpellColumn::~CHudSpellColumn() = default;

CHudActionColumn::~CHudActionColumn() {
    actions_.vtable = DwordArrayVtable();
    if (actions_.data) gamecrt::operator_delete(actions_.data);
}

CHudMap::~CHudMap() {
}

CHudUnitPanel::~CHudUnitPanel() = default;

// Only the two page texts (page 1 first) and the window's.
CHudTopPanel::~CHudTopPanel() {}
