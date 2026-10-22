// The interface windows by game.exe's table (C19): see harness_windows.h.
#include "harness_windows.h"

#include "../../src/game/ui.h"
#include "../../src/game/ui_briefing.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_item_info.h"
#include "../../src/game/ui_screens.h"
#include "../../src/game/ui_view_screen.h"
#include "ei/hook.h"
#include "game/game_data.h"

#include <typeinfo>
#include <vector>

namespace ei::verify {

namespace {

// A window class of the image with game.exe's table of it: a real C++ class by its type, one still built over a
// compiled tag (C28m) by the tag's table.
struct Kind {
    const void* original;          // game.exe's table: the label
    const void* compiled;          // the tag's table, or null
    const std::type_info* type;    // the class, or null
    Kind(const void* o, const void* c) : original(o), compiled(c), type(nullptr) {}
    Kind(const void* o, const std::type_info& t) : original(o), compiled(nullptr), type(&t) {}
};

const std::vector<Kind>& WindowKinds() {
    static const std::vector<Kind> kinds = {
        // the plain window, the controls, the chat, the icon window and the HUD panels (batch 19)
        {&ei::OriginalGameData<const std::uint8_t>(0x00740F60), typeid(CUiWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742C18), typeid(CUiScrollbar)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742CBC), typeid(CUiSlider)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742E9C), typeid(CUiScrollText)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742D5C), typeid(CUiSpin)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742DF4), typeid(CUiEditField)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742F64), typeid(CUiTooltip)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007400DC), typeid(CChatOverlay)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741478), typeid(CChatWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740444), typeid(CItemIconWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742498), typeid(CHudTopPanel)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742530), typeid(CHudMovePanel)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007425C8), typeid(CHudClock)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742660), typeid(CHudMap)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007426F8), typeid(CHudUnitPanel)},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074279C), typeid(CHudWeaponBar)},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074283C), typeid(CHudQuickItems)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007428E4), typeid(CHudSpellColumn)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742980), typeid(CHudActionColumn)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742A18), typeid(CHudPortraits)},
        // the containers and screens (batch 20)
        {&ei::OriginalGameData<const std::uint8_t>(0x00741008), typeid(CUiContainer)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740764), typeid(CBattleScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740688), typeid(CMainMenu)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007405AC), typeid(CBriefingScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740840), typeid(CViewScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740B3C), typeid(CGlobalMap)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740C3C), typeid(CCreditsScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740E28), typeid(CHelpWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007411BC), typeid(CObjectivesWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007412A4), typeid(CEscMenu)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741388), typeid(CSaveLoadWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741FAC), typeid(COptionsWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742348), typeid(CMessageBox)},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074225C), typeid(CDifficultyBox)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741DD8), typeid(CMasterRequestBox)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741CD0), typeid(CMpConnectScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740A2C), typeid(CItemScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074091C), typeid(CCampPages)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740D18), typeid(CExchangeScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074214C), typeid(CMpHeroScreen)},
        // The item screens' panels (ui_item_panel_tables.cpp).
        {&ei::OriginalGameData<const std::uint8_t>(0x00741B58), CUiItemPanel::BlankVtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x007404DC), CItemInfoPanel::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741ABC), CHeroStatsPanel::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x007418E0), CHeroEquipPanel::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741844), CHeroQuickStrip::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x007417A8), CTradePanel::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074170C), CTradeGrid::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741670), CSpellMakerPage::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x007415D4), CConstructorPage::ItemMakerVtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741538), CRepairGrid::Vtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741A1C), CUiItemStrip::PlainVtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074197C), CUiItemStrip::ForwardingVtable()},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741BF4), CUiItemStrip::FigureVtable()},
    };
    return kinds;
}

} // namespace

std::uintptr_t WindowKind(const void* window) {
    const std::uintptr_t vtable = *static_cast<const std::uintptr_t*>(window);
    if (!ei::Standalone()) return vtable;   // game.exe's windows keep game.exe's tables
    for (const Kind& kind : WindowKinds())
        if (kind.compiled && reinterpret_cast<std::uintptr_t>(kind.compiled) == vtable)
            return reinterpret_cast<std::uintptr_t>(kind.original);
    const std::type_info& type = typeid(*static_cast<const CUiWindow*>(window));
    for (const Kind& kind : WindowKinds())
        if (kind.type && *kind.type == type) return reinterpret_cast<std::uintptr_t>(kind.original);
    return vtable;
}

} // namespace ei::verify
