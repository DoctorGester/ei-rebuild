// Constructors of interface windows: the message box (00634650), the objectives window
// (0063B5D0) and the battle screen's portraits panel (0063FE70). Their Create / Build (slot 25)
// does the rest. See ui.h and docs/notes/ui.md.
#include "ui.h"

#include "console.h"
#include "item_db_text.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_ui.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/mfc_slots.h"

#include <new>

namespace {

void** MessageBoxVtable() { return ei::GamePointer<void*>(0x00742348); }
void** MessageBoxInputVtable() { return ei::GamePointer<void*>(0x00742314); }
void** ObjectivesVtable() { return ei::GamePointer<void*>(0x007411BC); }
void** ObjectivesInputVtable() { return ei::GamePointer<void*>(0x00741188); }
void* ObjectiveExitsVtable() { return mfc::impl::ObjectiveExitArrayClass::Vtable(); }
void** PortraitsVtable() { return ei::GamePointer<void*>(0x00742A18); }
void** PortraitsInputVtable() { return ei::GamePointer<void*>(0x007429E4); }

constexpr std::uint32_t kEditField = 4;   // the message box's flag: an edit field instead of the text

// A CString member constructed empty.
mfc::CString* EmptyString(mfc::CString* field) { return ::new (field) mfc::CString; }

} // namespace

// --- 00634650: the message box ------------------------------------------------------------------

CMessageBox* CMessageBox::Construct(CInterface3D* ui, const char* titleKey, const char* textKey, std::uint32_t result,
                                    std::uint32_t flags, std::uint32_t format) {
    CUiContainer::Construct(ui);
    EmptyString(&title_);
    EmptyString(&text_);
    result_ = result;
    flags_ = flags;
    format_ = format;
    vtable_ = MessageBoxVtable();
    inputVtable_ = MessageBoxInputVtable();
    mfc::CString text;
    CTextManager* texts = TheTextManager();
    texts->GetString(titleKey, &text);
    title_ = text;
    if (flags_ & kEditField) {
        editText_ = const_cast<char*>(textKey);   // the caller's buffer: OK copies the answer into it
    } else {
        editText_ = nullptr;
        texts->GetString(textKey, &text);
        text_ = text;
    }
    return this;
}
EI_REWRITTEN(0x00634650, CMessageBox::Construct);

// --- 0063B5D0: the objectives window -------------------------------------------------------------

CObjectivesWindow* CObjectivesWindow::Construct(CInterface3D* ui) {
    CUiContainer::Construct(ui);
    zones_.Start() = nullptr;
    zones_.Finish() = nullptr;
    zones_.EndOfStorage() = nullptr;
    EmptyString(&zoneName_);
    exitDistances_ = nullptr;
    EmptyString(&shownZone_);
    exits_.vtable = ObjectiveExitsVtable();
    exits_.data = nullptr;
    exits_.growBy = 0;
    exits_.maxSize = 0;
    exits_.size = 0;
    quests_.Construct();
    questPlaces_.Start() = nullptr;
    questPlaces_.Finish() = nullptr;
    questPlaces_.EndOfStorage() = nullptr;
    objectiveTexts_.Construct();
    vtable_ = ObjectivesVtable();
    inputVtable_ = ObjectivesInputVtable();
    return this;
}
EI_REWRITTEN(0x0063B5D0, CObjectivesWindow::Construct);

// --- 0063FE70: the portraits panel ---------------------------------------------------------------

CHudPortraits* CHudPortraits::Construct(CInterface3D* ui, CBattleScreen* owner) {
    CUiWindow::Construct(ui);
    owner_ = owner;
    slots_ = nullptr;
    slotsEnd_ = nullptr;
    slotsCapacity_ = nullptr;
    vtable_ = PortraitsVtable();
    inputVtable_ = PortraitsInputVtable();
    rect_ = RECT{250, 440, 550, 600};
    count_ = 0;
    AddSprite(0)->SetRectangle(10.0f, 80.0f, 0.0f, 0.0f, 20.0f, 20.0f, 0x22, 0x96, 0x36, 0xAA);
    return this;
}
EI_REWRITTEN(0x0063FE70, CHudPortraits::Construct);

// --- 0063FF70: its destructor body -----------------------------------------------------------------

void CHudPortraits::DestroyBody() {
    for (HudPortraitSlot* slot = slots_; slot != slotsEnd_; ++slot) {
        slot->modelSuffix_.~CString();
        slot->modelPrefix_.~CString();
        slot->faceName_.~CString();
    }
    if (slots_) stlp::deallocate(slots_, static_cast<std::size_t>(slotsCapacity_ - slots_) * sizeof(HudPortraitSlot));
    DestroyBase();
}
EI_REWRITTEN(0x0063FF70, CHudPortraits::DestroyBody);
