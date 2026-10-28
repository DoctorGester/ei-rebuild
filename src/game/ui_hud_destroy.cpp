// Destructors of the battle HUD's panels,
// with the deleting destructors and the input sides' copies. See ui.h
// and docs/notes/ui.md.
#include "ui.h"

#include "ei/hook.h"
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
EI_REWRITTEN_SPECIAL(0x0063BC70, CHudWeaponBar::~CHudWeaponBar);
EI_REWRITTEN_SPECIAL(0x0063BC50, CHudWeaponBar::~CHudWeaponBar);
EI_REWRITTEN_SPECIAL(0x0064BCF0, CHudWeaponBar::~CHudWeaponBar);

CHudQuickItems::~CHudQuickItems() {
    entries_.vtable = QuickEntriesVtable();
    if (entries_.data) gamecrt::operator_delete(entries_.data);
}
EI_REWRITTEN_SPECIAL(0x0063CA70, CHudQuickItems::~CHudQuickItems);
EI_REWRITTEN_SPECIAL(0x0063CA50, CHudQuickItems::~CHudQuickItems);
EI_REWRITTEN_SPECIAL(0x0064BD00, CHudQuickItems::~CHudQuickItems);

CHudSpellColumn::~CHudSpellColumn() = default;
EI_REWRITTEN_SPECIAL(0x0063E460, CHudSpellColumn::~CHudSpellColumn);
EI_REWRITTEN_SPECIAL(0x0063E440, CHudSpellColumn::~CHudSpellColumn);
EI_REWRITTEN_SPECIAL(0x0064BD10, CHudSpellColumn::~CHudSpellColumn);

CHudActionColumn::~CHudActionColumn() {
    actions_.vtable = DwordArrayVtable();
    if (actions_.data) gamecrt::operator_delete(actions_.data);
}
EI_REWRITTEN_SPECIAL(0x0063F490, CHudActionColumn::~CHudActionColumn);
EI_REWRITTEN_SPECIAL(0x0063F470, CHudActionColumn::~CHudActionColumn);
EI_REWRITTEN_SPECIAL(0x0064BD20, CHudActionColumn::~CHudActionColumn);

CHudMap::~CHudMap() {
    if (visibleArea_.data) lib::FreeAligned32(visibleArea_.data);
    if (viewPolygon_.data) lib::FreeAligned32(viewPolygon_.data);
}
EI_REWRITTEN_SPECIAL(0x00635CA0, CHudMap::~CHudMap);
EI_REWRITTEN_SPECIAL(0x00635C80, CHudMap::~CHudMap);
EI_REWRITTEN_SPECIAL(0x0064BD50, CHudMap::~CHudMap);

// The two strings go after the body (strings_[1] first).
CHudUnitPanel::~CHudUnitPanel() {
    partLabels_.Destruct();
    texts_.Destruct();
}
EI_REWRITTEN_SPECIAL(0x00635D00, CHudUnitPanel::~CHudUnitPanel);
EI_REWRITTEN_SPECIAL(0x00635CE0, CHudUnitPanel::~CHudUnitPanel);
EI_REWRITTEN_SPECIAL(0x0064BD40, CHudUnitPanel::~CHudUnitPanel);

// Only the two page texts (page 1 first) and the window's.
CHudTopPanel::~CHudTopPanel() {}
EI_REWRITTEN_SPECIAL(0x00635E00, CHudTopPanel::~CHudTopPanel);
EI_REWRITTEN_SPECIAL(0x00635DE0, CHudTopPanel::~CHudTopPanel);
EI_REWRITTEN_SPECIAL(0x0064BD80, CHudTopPanel::~CHudTopPanel);
