// Destructor bodies of the battle HUD's panels (0063BC70, 0063CA70, 0063E460, 0063F490); each
// panel's slot 0 calls its body and then frees the window. See ui.h and docs/notes/ui.md.
#include "ui.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_library.h"

namespace {
// The vtables the inlined MFC array destructors write before freeing the storage.
void* QuickEntriesVtable() { return reinterpret_cast<void*>(0x007428A0); }   // CArray<Entry>
void* DwordArrayVtable() { return reinterpret_cast<void*>(0x0073C3D0); }     // CDWordArray
} // namespace

void CHudWeaponBar::DestroyBody() {
    StringArrayDestruct(&labelsVtable_);
    DestroyBase();
}
EI_REPLACE_METHOD(0x0063BC70, CHudWeaponBar::DestroyBody);

void CHudQuickItems::DestroyBody() {
    StringArrayDestruct(&labelsVtable_);
    entriesVtable_ = QuickEntriesVtable();
    if (entries_) gamecrt::operator_delete(entries_);
    DestroyBase();
}
EI_REPLACE_METHOD(0x0063CA70, CHudQuickItems::DestroyBody);

void CHudSpellColumn::DestroyBody() {
    StringArrayDestruct(&labelsVtable_);
    DestroyBase();
}
EI_REPLACE_METHOD(0x0063E460, CHudSpellColumn::DestroyBody);

void CHudActionColumn::DestroyBody() {
    StringArrayDestruct(&labelsVtable_);
    actions_.vtable_ = DwordArrayVtable();
    if (actions_.data_) gamecrt::operator_delete(actions_.data_);
    DestroyBase();
}
EI_REPLACE_METHOD(0x0063F490, CHudActionColumn::DestroyBody);
