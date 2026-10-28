// A region's tooltip (CUiRegion, ui.h): made from a text id ("tip <id>" in the texts)
// or a string, its text changed, and the region's contents released
//. Every screen and HUD panel builder gives its regions tooltips. See
// docs/notes/ui.md ("Interface manager and window base").
#include "ui.h"

#include "item_db_text.h"
#include "text_reader.h"

#include "lib/crt.h"
#include <string>
#include "game_singletons.h"
#include "lib/str.h"


namespace {

constexpr const char* kTipText = "tip %d";

// A new tooltip of `region` (null when the allocation fails).
CUiTooltip* NewTooltip(CUiRegion* region, std::string_view text, int flag, int extra) {
    return new CUiTooltip(region->interface_, region, text, flag, extra);
}

} // namespace

// --- A new tooltip --------------------------------------------------------

void CUiRegion::SetTip(int textId, int flag, int extra) {
    if (textId == 0) {
        delete tip_;
        tip_ = NewTooltip(this, "", 1, 0);
        return;
    }
    std::string text;
    text = str::Format(kTipText, textId);
    CTextManager* texts = TheTextManager();
    if (texts->Open(text.c_str())) texts->ReadToHash(&text, 1);
    delete tip_;
    tip_ = NewTooltip(this, text, flag, extra);
}

void CUiRegion::SetTipString(std::string_view text, int flag, int extra) {
    delete tip_;
    tip_ = NewTooltip(this, text, flag, extra);
}

// --- The tooltip's text --------------------------------------------------------------

void CUiRegion::SetTipText(std::string_view text) {
    if (tip_) tip_->SetText(text);
}

// --- The destructor body --------------------------------------------------------------

CUiRegion::~CUiRegion() { delete tip_; }
