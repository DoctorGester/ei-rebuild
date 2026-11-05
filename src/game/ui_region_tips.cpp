// A region's tooltip (UiRegion, ui.h): made from a text id ("tip <id>" in the texts) or a string, and its text
// changed. Every screen and HUD panel builder gives its regions tooltips. See
// docs/notes/ui.md ("Interface manager and window base").
#include "game/ui.h"

#include "game/text_manager.h"

#include "lib/crt.h"
#include <string>
#include "game/game_singletons.h"
#include "game/world.h"
#include "lib/str.h"


namespace {
constexpr const char* kTipText = "tip %d";
} // namespace

// --- A new tooltip --------------------------------------------------------

void UiRegion::SetTip(int textId, int command) {
    if (textId == 0) {
        tip = std::make_unique<UiTooltip>(this, "", 0);
        return;
    }
    std::string text = str::Format(kTipText, textId);
    TextManager* texts = g_clientWorld.texts.get();
    if (texts->Open(text)) text = texts->ReadToHash();
    tip = std::make_unique<UiTooltip>(this, text, command);
}

void UiRegion::SetTipString(std::string_view text, int command) { tip = std::make_unique<UiTooltip>(this, text, command); }

// --- The tooltip's text --------------------------------------------------------------

void UiRegion::SetTipText(std::string_view text) {
    if (tip) tip->SetText(text);
}
