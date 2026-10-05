// Constructors of interface windows: the message box (00634650), the objectives window
// (0063B5D0) and the battle screen's portraits panel (0063FE70). Their Create / Build (slot 25)
// does the rest. See ui.h and docs/notes/ui.md.
#include "ui.h"

#include "console.h"
#include "item_db_text.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_ui.h"

#include <new>

namespace {

void** MessageBoxVtable() { return reinterpret_cast<void**>(0x00742348); }
void** MessageBoxInputVtable() { return reinterpret_cast<void**>(0x00742314); }
void** ObjectivesVtable() { return reinterpret_cast<void**>(0x007411BC); }
void** ObjectivesInputVtable() { return reinterpret_cast<void**>(0x00741188); }
void* ObjectiveExitsVtable() { return reinterpret_cast<void*>(0x00741264); }
void** PortraitsVtable() { return reinterpret_cast<void**>(0x00742A18); }
void** PortraitsInputVtable() { return reinterpret_cast<void**>(0x007429E4); }

constexpr std::uint32_t kEditField = 4;   // the message box's flag: an edit field instead of the text

// A CString member the layouts keep as its data pointer, constructed empty.
template <typename T>
mfc::CString* EmptyString(T* field) {
    return new (static_cast<void*>(field)) mfc::CString;
}
template <typename T>
mfc::CString& AsString(T* field) {
    return *reinterpret_cast<mfc::CString*>(field);
}

} // namespace

// --- 00634650: the message box ------------------------------------------------------------------

CMessageBox* CMessageBox::Construct(CInterface3D* ui, const char* titleKey, const char* textKey, std::uint32_t result,
                                    std::uint32_t flags, std::uint32_t format) {
    ConstructContainerAt(this, nullptr, ui);
    EmptyString(&title_);
    EmptyString(&text_);
    result_ = result;
    flags_ = flags;
    format_ = format;
    vtable_ = MessageBoxVtable();
    inputVtable_ = MessageBoxInputVtable();
    mfc::CString text;
    CTextManager* texts = GameTexts();
    texts->GetString(titleKey, &text);
    AsString(&title_) = text;
    if (flags_ & kEditField) {
        editText_ = textKey;
    } else {
        editText_ = nullptr;
        texts->GetString(textKey, &text);
        AsString(&text_) = text;
    }
    return this;
}
EI_REPLACE_METHOD(0x00634650, CMessageBox::Construct);

// --- 0063B5D0: the objectives window -------------------------------------------------------------

CObjectivesWindow* CObjectivesWindow::Construct(CInterface3D* ui) {
    ConstructContainerAt(this, nullptr, ui);
    zonesBegin_ = nullptr;
    zonesEnd_ = nullptr;
    zonesCapacity_ = nullptr;
    EmptyString(&zoneName_);
    exitDistances_ = nullptr;
    EmptyString(&shownZone_);
    exitsVtable_ = ObjectiveExitsVtable();
    exits_ = nullptr;
    exitGrowBy_ = 0;
    exitCapacity_ = 0;
    exitCount_ = 0;
    new (static_cast<void*>(quests_)) console::CStringArray;
    questPlacesBegin_ = nullptr;
    questPlacesEnd_ = nullptr;
    questPlacesCapacity_ = nullptr;
    new (static_cast<void*>(objectiveTexts_)) console::CStringArray;
    vtable_ = ObjectivesVtable();
    inputVtable_ = ObjectivesInputVtable();
    return this;
}
EI_REPLACE_METHOD(0x0063B5D0, CObjectivesWindow::Construct);

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
EI_REPLACE_METHOD(0x0063FE70, CHudPortraits::Construct);
