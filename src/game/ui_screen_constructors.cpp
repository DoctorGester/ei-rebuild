// Constructors of interface windows: the message box, the objectives window
// and the battle screen's portraits panel. Their Create / Build
// does the rest. See ui.h and docs/notes/ui.md.
#include "ui.h"

#include "console.h"
#include "text_manager.h"

#include <cstdint>
#include <string>
#include "game_singletons.h"
#include "world.h"

#include <new>

namespace {

constexpr std::uint32_t kEditField = 4;   // the message box's flag: an edit field instead of the text

} // namespace

// --- The message box ----------------------------------------------------------------------------------

MessageWindow::MessageWindow(Interface3D* ui, std::string_view titleKey, const char* textKey, std::uint32_t newResult,
                         std::uint32_t newFlags, std::uint32_t newFormat)
    : UiContainer(ui), result(newResult), flags(newFlags), format(newFormat) {
    std::string line;
    TextManager* texts = g_clientWorld.texts;
    line = texts->GetString(titleKey);
    title = line;
    if (flags & kEditField) {
        editText = const_cast<char*>(textKey);    // the caller's buffer: OK copies the answer into it
    } else {
        editText = nullptr;
        line = texts->GetString(textKey);
        text = line;
    }
}

// --- The objectives window ----------------------------------------------------------------------------

// --- The portraits panel ------------------------------------------------------------------------------

HudPortraits::HudPortraits(Interface3D* ui, BattleScreen* newOwner) : UiWindow(ui), owner(newOwner) {
    rect = Rect{250, 440, 550, 600};
    count = 0;
    AddBlankSprite()->SetRectangle(10.0f, 80.0f, 0.0f, 0.0f, 20.0f, 20.0f, 0x22, 0x96, 0x36, 0xAA);
}
