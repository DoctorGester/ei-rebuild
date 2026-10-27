// A region's tooltip (CUiRegion, ui.h): made from a text id ("tip <id>" in the texts, 005FC730)
// or a string, its text changed, and the region's contents released
//. Every screen and HUD panel builder gives its regions tooltips. See
// docs/notes/ui.md ("Interface manager and window base").
#include "ui.h"

#include "item_db_text.h"
#include "text_reader.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "game_singletons.h"

namespace {

constexpr const char* kTipText = "tip %d";

// A new tooltip of `region` (null when the allocation fails, as in the original).
CUiTooltip* NewTooltip(CUiRegion* region, const char* text, int flag, int extra) {
    return new CUiTooltip(region->interface_, region, text, flag, extra);
}

} // namespace

// --- 005FC730 / 005FC6B0: a new tooltip --------------------------------------------------------

void CUiRegion::SetTip(int textId, int flag, int extra) {
    if (textId == 0) {
        delete tip_;
        tip_ = NewTooltip(this, "", 1, 0);
        return;
    }
    mfc::CString text;
    text.Format(kTipText, textId);
    CTextManager* texts = TheTextManager();
    if (texts->Open(text)) texts->ReadToHash(&text, 1);
    delete tip_;
    tip_ = NewTooltip(this, text, flag, extra);
}
EI_REWRITTEN(0x005FC730, CUiRegion::SetTip);

void CUiRegion::SetTipString(const char* text, int flag, int extra) {
    delete tip_;
    tip_ = NewTooltip(this, text, flag, extra);
}
EI_REWRITTEN(0x005FC6B0, CUiRegion::SetTipString);

// --- 005FC690: the tooltip's text --------------------------------------------------------------

void CUiRegion::SetTipText(const char* text) {
    if (tip_) tip_->SetText(text);
}
EI_REWRITTEN(0x005FC690, CUiRegion::SetTipText);

// --- 005FC600: the destructor body --------------------------------------------------------------

void CUiRegion::ReleaseContents() {
    delete tip_;
    points_.FreeData();
}
EI_REWRITTEN(0x005FC600, CUiRegion::ReleaseContents);
