// A region's tooltip (CUiRegion, ui.h): made from a text id ("tip <id>" in the texts)
// or a string, its text changed, and the region's contents released
//. Every screen and HUD panel builder gives its regions tooltips. See
// docs/notes/ui.md ("Interface manager and window base").
#include "ui.h"

#include "text_manager.h"

#include "lib/crt.h"
#include <string>
#include "game_singletons.h"
#include "world.h"
#include "lib/str.h"


namespace {

constexpr const char* kTipText = "tip %d";

// A new tooltip of `region` (null when the allocation fails).
CUiTooltip* NewTooltip(CUiRegion* region, std::string_view text, int command) {
    return new CUiTooltip(region->interface_, region, text, command);
}

} // namespace

// --- A new tooltip --------------------------------------------------------

void CUiRegion::SetTip(int textId, int command) {
    if (textId == 0) {
        delete tip_;
        tip_ = NewTooltip(this, "", 0);
        return;
    }
    std::string text = str::Format(kTipText, textId);
    CTextManager* texts = g_clientWorld.texts_;
    if (texts->Open(text)) text = texts->ReadToHash();
    delete tip_;
    tip_ = NewTooltip(this, text, command);
}

void CUiRegion::SetTipString(std::string_view text, int command) {
    delete tip_;
    tip_ = NewTooltip(this, text, command);
}

// --- The tooltip's text --------------------------------------------------------------

void CUiRegion::SetTipText(std::string_view text) {
    if (tip_) tip_->SetText(text);
}

// --- The destructor body --------------------------------------------------------------

CUiRegion::~CUiRegion() { delete tip_; }
