// A region's tooltip (UiRegion, ui.h): made from a text id ("tip <id>" in the texts)
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
UiTooltip* NewTooltip(UiRegion* region, std::string_view text, int command) {
    return new UiTooltip(region->ui, region, text, command);
}

} // namespace

// --- A new tooltip --------------------------------------------------------

void UiRegion::SetTip(int textId, int command) {
    if (textId == 0) {
        delete tip;
        tip = NewTooltip(this, "", 0);
        return;
    }
    std::string text = str::Format(kTipText, textId);
    TextManager* texts = g_clientWorld.texts;
    if (texts->Open(text)) text = texts->ReadToHash();
    delete tip;
    tip = NewTooltip(this, text, command);
}

void UiRegion::SetTipString(std::string_view text, int command) {
    delete tip;
    tip = NewTooltip(this, text, command);
}

// --- The tooltip's text --------------------------------------------------------------

void UiRegion::SetTipText(std::string_view text) {
    if (tip) tip->SetText(text);
}

// --- The destructor body --------------------------------------------------------------

UiRegion::~UiRegion() { delete tip; }
