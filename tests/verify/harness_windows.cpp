// The interface windows by game.exe's table (C19): see harness_windows.h.
#include "harness_windows.h"

#include "../../src/game/ui.h"
#include "../../src/game/ui_briefing.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_item_info.h"
#include "../../src/game/ui_screens.h"
#include "../../src/game/ui_view_screen.h"
#include "game/game_data.h"

#include <vector>

namespace ei::verify {

namespace {

// A compiled window table of the image with game.exe's table it stands for.
struct WindowKind {
    const void* original;   // game.exe's table: the label
    const void* compiled;   // the image's
};

const std::vector<WindowKind>& WindowKinds() {
    static const std::vector<WindowKind> kinds = {
        // the plain window, the controls, the chat, the icon window and the HUD panels (batch 19)
        {&ei::OriginalGameData<const std::uint8_t>(0x00740F60), CUiWindow::PlainVtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742C18), CUiScrollbar::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742CBC), CUiSlider::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742E9C), CUiScrollText::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742D5C), CUiSpin::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742DF4), CUiEditField::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742F64), CUiTooltip::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x007400DC), CChatOverlay::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741478), CChatWindow::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740444), CItemIconWindow::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742498), CHudTopPanel::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742530), CHudMovePanel::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x007425C8), CHudClock::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742660), CHudMap::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x007426F8), CHudUnitPanel::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074279C), CHudWeaponBar::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074283C), CHudQuickItems::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x007428E4), CHudSpellColumn::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742980), CHudActionColumn::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742A18), CHudPortraits::Vtable()},
        // the containers and screens (batch 20)
        {&ei::OriginalGameData<const std::uint8_t>(0x00741008), CUiContainer::ContainerVtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740764), CBattleScreen::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740688), CMainMenu::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x007405AC), CBriefingScreen::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740840), CViewScreen::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740B3C), CGlobalMap::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740C3C), CCreditsScreen::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740E28), CHelpWindow::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x007411BC), CObjectivesWindow::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x007412A4), CEscMenu::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741388), CSaveLoadWindow::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741FAC), COptionsWindow::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742348), CMessageBox::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074225C), CDifficultyBox::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741DD8), CMasterRequestBox::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741CD0), CMpConnectScreen::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740A2C), CItemScreen::ItemScreenVtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074091C), CCampPages::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740D18), CExchangeScreen::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074214C), CMpHeroScreen::Vtable()},
    };
    return kinds;
}

} // namespace

std::uintptr_t RealWindowVtable(std::uintptr_t vtable) {
    for (const WindowKind& kind : WindowKinds())
        if (reinterpret_cast<std::uintptr_t>(kind.compiled) == vtable) return reinterpret_cast<std::uintptr_t>(kind.original);
    return vtable;
}

} // namespace ei::verify
