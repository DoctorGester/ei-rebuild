// Destructor bodies of the battle HUD's panels (00635CA0, 00635D00, 00635E00, 0063BC70, 0063CA70,
// 0063E460, 0063F490); each panel's slot 0 calls its body and then frees the window. See ui.h and
// docs/notes/ui.md.
#include "ui.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "game_data.h"

namespace {
// The vtables the inlined MFC array destructors write before freeing the storage.
void* QuickEntriesVtable() { return ei::GamePointer<void>(0x007428A0); }   // CArray<Entry>
void* DwordArrayVtable() { return ei::GamePointer<void>(0x0073C3D0); }     // CDWordArray
} // namespace

void CHudWeaponBar::DestroyBody() {
    labels_.Destruct();
    DestroyBase();
}
EI_REPLACE_METHOD(0x0063BC70, CHudWeaponBar::DestroyBody);

void CHudQuickItems::DestroyBody() {
    labels_.Destruct();
    entriesVtable_ = QuickEntriesVtable();
    if (entries_) gamecrt::operator_delete(entries_);
    DestroyBase();
}
EI_REPLACE_METHOD(0x0063CA70, CHudQuickItems::DestroyBody);

void CHudSpellColumn::DestroyBody() {
    labels_.Destruct();
    DestroyBase();
}
EI_REPLACE_METHOD(0x0063E460, CHudSpellColumn::DestroyBody);

void CHudActionColumn::DestroyBody() {
    labels_.Destruct();
    actions_.vtable = DwordArrayVtable();
    if (actions_.data) gamecrt::operator_delete(actions_.data);
    DestroyBase();
}
EI_REPLACE_METHOD(0x0063F490, CHudActionColumn::DestroyBody);

void CHudMap::DestroyBody() {
    if (visibleArea_.data) FreeAligned(visibleArea_.data);
    if (viewPolygon_.data) FreeAligned(viewPolygon_.data);
    DestroyBase();
}
EI_REPLACE_METHOD(0x00635CA0, CHudMap::DestroyBody);

void CHudUnitPanel::DestroyBody() {
    partLabels_.Destruct();
    texts_.Destruct();
    strings_[1].~CString();
    strings_[0].~CString();
    DestroyBase();
}
EI_REPLACE_METHOD(0x00635D00, CHudUnitPanel::DestroyBody);

void CHudTopPanel::DestroyBody() {
    pageTexts_[1].~CString();
    pageTexts_[0].~CString();
    DestroyBase();
}
EI_REPLACE_METHOD(0x00635E00, CHudTopPanel::DestroyBody);
