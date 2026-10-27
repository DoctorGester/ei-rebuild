// Constructors of interface windows: the message box, the objectives window
// and the battle screen's portraits panel. Their Create / Build (slot 25)
// does the rest. See ui.h and docs/notes/ui.md.
#include "ui.h"

#include "console.h"
#include "item_db_text.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/mfc_slots.h"

#include <new>

namespace {

void* ObjectiveExitsVtable() { return mfc::impl::ObjectiveExitArrayClass::Vtable(); }

constexpr std::uint32_t kEditField = 4;   // the message box's flag: an edit field instead of the text

} // namespace

// --- 00634650: the message box ------------------------------------------------------------------

CMessageBox::CMessageBox(CInterface3D* ui, const char* titleKey, const char* textKey, std::uint32_t result,
                         std::uint32_t flags, std::uint32_t format)
    : CUiContainer(ui), result_(result), flags_(flags), format_(format) {
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
}
EI_REWRITTEN_SPECIAL(0x00634650, CMessageBox::CMessageBox);

// --- 0063B5D0: the objectives window -------------------------------------------------------------

CObjectivesWindow::CObjectivesWindow(CInterface3D* ui) : CUiContainer(ui) {
    exitDistances_ = nullptr;
    exits_.vtable = ObjectiveExitsVtable();
    exits_.data = nullptr;
    exits_.growBy = 0;
    exits_.maxSize = 0;
    exits_.size = 0;
    quests_.Construct();
    objectiveTexts_.Construct();
}
EI_REWRITTEN_SPECIAL(0x0063B5D0, CObjectivesWindow::CObjectivesWindow);

// --- 0063FE70: the portraits panel ---------------------------------------------------------------

CHudPortraits::CHudPortraits(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {
    rect_ = RECT{250, 440, 550, 600};
    count_ = 0;
    AddSprite(0)->SetRectangle(10.0f, 80.0f, 0.0f, 0.0f, 20.0f, 20.0f, 0x22, 0x96, 0x36, 0xAA);
}
EI_REWRITTEN_SPECIAL(0x0063FE70, CHudPortraits::CHudPortraits);

// --- 0063FF70: its destructor ----------------------------------------------------------------------

CHudPortraits::~CHudPortraits() {
    for (HudPortraitSlot* slot = slots_.begin(); slot != slots_.end(); ++slot) slot->~HudPortraitSlot();
    slots_.FreeStorage();
}
EI_REWRITTEN_SPECIAL(0x0063FF70, CHudPortraits::~CHudPortraits);
EI_REWRITTEN_SPECIAL(0x0063FF50, CHudPortraits::~CHudPortraits);   // deleting
EI_REWRITTEN_SPECIAL(0x0064BD30, CHudPortraits::~CHudPortraits);   // the input side's
