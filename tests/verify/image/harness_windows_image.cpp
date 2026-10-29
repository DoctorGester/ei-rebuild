// The interface windows in the image by game.exe's table (C19; image_side.h, harness_windows.h).
#include "image_side.h"

#include "../harness_original.h"

#include "game/game_singletons.h"
#include "game/ui.h"
#include "game/ui_briefing.h"
#include "game/ui_camp.h"
#include "game/ui_item_info.h"
#include "game/ui_screens.h"
#include "game/ui_view_screen.h"

#include <typeinfo>
#include <vector>

namespace ei::verify::image {

namespace {

// A window class of the image with game.exe's table of it.
struct Kind {
    const void* original;          // game.exe's table: the label
    const std::type_info* type;    // the class
    Kind(const void* o, const std::type_info& t) : original(o), type(&t) {}
};

const std::vector<Kind>& WindowKinds() {
    static const std::vector<Kind> kinds = {
        // the plain window, the controls, the chat, the icon window and the HUD panels (batch 19)
        {&ei::OriginalGameData<const std::uint8_t>(0x00740F60), typeid(UiWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742C18), typeid(UiScrollbar)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742CBC), typeid(UiSlider)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742E9C), typeid(UiScrollText)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742D5C), typeid(UiSpin)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742DF4), typeid(UiEditField)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742F64), typeid(UiTooltip)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007400DC), typeid(ChatOverlay)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741478), typeid(ChatWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740444), typeid(ItemIconWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742498), typeid(HudTopPanel)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742530), typeid(HudMovePanel)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007425C8), typeid(HudClock)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742660), typeid(HudMap)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007426F8), typeid(HudUnitPanel)},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074279C), typeid(HudWeaponBar)},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074283C), typeid(HudQuickItems)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007428E4), typeid(HudSpellColumn)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742980), typeid(HudActionColumn)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742A18), typeid(HudPortraits)},
        // the containers and screens (batch 20)
        {&ei::OriginalGameData<const std::uint8_t>(0x00741008), typeid(UiContainer)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740764), typeid(BattleScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740688), typeid(MainMenu)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007405AC), typeid(BriefingScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740840), typeid(ViewScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740B3C), typeid(GlobalMap)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740C3C), typeid(CreditsScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740E28), typeid(HelpWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007411BC), typeid(ObjectivesWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007412A4), typeid(EscMenu)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741388), typeid(SaveLoadWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741FAC), typeid(OptionsWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00742348), typeid(MessageWindow)},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074225C), typeid(DifficultyBox)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741DD8), typeid(MasterRequestBox)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741CD0), typeid(MpConnectScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740A2C), typeid(ItemScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074091C), typeid(CampPages)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00740D18), typeid(ExchangeScreen)},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074214C), typeid(MpHeroScreen)},
        // the item screens' panels and strips
        {&ei::OriginalGameData<const std::uint8_t>(0x00741B58), typeid(UiItemPanel)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007404DC), typeid(ItemInfoPanel)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741ABC), typeid(HeroStatsPanel)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007418E0), typeid(HeroEquipPanel)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741844), typeid(HeroQuickStrip)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007417A8), typeid(TradePanel)},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074170C), typeid(TradeGrid)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741670), typeid(SpellMakerPage)},
        {&ei::OriginalGameData<const std::uint8_t>(0x007415D4), typeid(ConstructorPage)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741538), typeid(RepairGrid)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741A1C), typeid(UiItemStrip)},
        {&ei::OriginalGameData<const std::uint8_t>(0x0074197C), typeid(ForwardingStrip)},
        {&ei::OriginalGameData<const std::uint8_t>(0x00741BF4), typeid(FigureStrip)},
    };
    return kinds;
}

} // namespace

void* TopWindow() { return g_interface.GetActiveWindow(); }

std::uintptr_t WindowKind(const void* window) {
    const std::uintptr_t vtable = *static_cast<const std::uintptr_t*>(window);
    const std::type_info& type = typeid(*static_cast<const UiWindow*>(window));
    for (const Kind& kind : WindowKinds())
        if (*kind.type == type) return reinterpret_cast<std::uintptr_t>(kind.original);
    return vtable;
}

} // namespace ei::verify::image
