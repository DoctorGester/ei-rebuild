// A region's tooltip (CUiRegion, ui.h): made from a text id ("tip <id>" in the texts, 005FC730)
// or a string (005FC6B0), its text changed (005FC690), and the region's contents released
// (005FC600). Every screen and HUD panel builder gives its regions tooltips. See
// docs/notes/ui.md ("Interface manager and window base").
#include "ui.h"

#include "item_db_text.h"
#include "text_reader.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_ui.h"
#include "game_singletons.h"

namespace {

constexpr const char* kTipText = "tip %d";   // 0079321C
constexpr std::size_t kTooltipSize = 0x184;

// A new tooltip of `region` (null when the allocation fails, as in the original).
CUiTooltip* NewTooltip(CUiRegion* region, const char* text, int flag, int extra) {
    void* memory = gamecrt::operator_new(kTooltipSize);
    if (!memory) return nullptr;
    return static_cast<CUiTooltip*>(memory)->Construct(region->interface_, region, text, flag, extra);
}

} // namespace

// --- 005FC730 / 005FC6B0: a new tooltip --------------------------------------------------------

void CUiRegion::SetTip(int textId, int flag, int extra) {
    if (textId == 0) {
        if (tip_) tip_->Delete();
        tip_ = NewTooltip(this, "", 1, 0);
        return;
    }
    mfc::CString text;
    text.Format(kTipText, textId);
    CTextManager* texts = TheTextManager();
    if (texts->Open(text)) texts->ReadToHash(&text, 1);
    if (tip_) tip_->Delete();
    tip_ = NewTooltip(this, text, flag, extra);
}
EI_REWRITTEN(0x005FC730, CUiRegion::SetTip);

void CUiRegion::SetTipString(const char* text, int flag, int extra) {
    if (tip_) tip_->Delete();
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
    if (tip_) tip_->Delete();
    points_.FreeData();
}
EI_REWRITTEN(0x005FC600, CUiRegion::ReleaseContents);
