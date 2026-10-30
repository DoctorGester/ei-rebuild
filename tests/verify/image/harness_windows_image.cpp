// The interface windows in the image by name (image_side.h, harness_windows.h).
#include "image_side.h"

#include "game/game_singletons.h"
#include "game/ui.h"
#include "game/ui_briefing.h"
#include "game/ui_camp.h"
#include "game/ui_item_info.h"
#include "game/ui_screens.h"
#include "game/ui_view_screen.h"

#include <cstring>
#include <string>
#include <typeinfo>
#include <vector>

namespace ei::verify::image {

namespace {

// The windows a scenario names (waitwindow, harness.cpp) and their classes.
struct Named {
    const char* name;
    const std::type_info* type;
};

const std::vector<Named>& NamedWindows() {
    static const std::vector<Named> windows = {
        {"menu", &typeid(MainMenu)},                // CreateScreen 2
        {"battle", &typeid(BattleScreen)},          // the in-game screen (CreateScreen 1)
        {"briefing", &typeid(BriefingScreen)},      // the base / briefing screen (CreateScreen 3)
        {"map", &typeid(GlobalMap)},                // the island map (CreateScreen 4)
        {"camp", &typeid(CampPages)},               // CreateScreen 5
        {"view", &typeid(ViewScreen)},              // CreateScreen 7
        {"trade", &typeid(ExchangeScreen)},         // trading between players (CreateScreen 9)
        {"escmenu", &typeid(EscMenu)},
        {"saveload", &typeid(SaveLoadWindow)},
        {"options", &typeid(OptionsWindow)},
        {"objectives", &typeid(ObjectivesWindow)},
        {"help", &typeid(HelpWindow)},              // the help / message window
        {"msgbox", &typeid(MessageWindow)},
        {"difficulty", &typeid(DifficultyBox)},
        {"heroes", &typeid(MpHeroScreen)},          // the multiplayer hero screen (main menu region 5)
        {"titles", &typeid(CreditsScreen)},
        {"mpconnect", &typeid(MpConnectScreen)},    // the multiplayer connect screen
    };
    return windows;
}

} // namespace

void* TopWindow() { return g_interface.GetActiveWindow(); }

std::string WindowKind(const void* window) {
    const std::type_info& type = typeid(*static_cast<const UiWindow*>(window));
    for (const Named& named : NamedWindows())
        if (*named.type == type) return named.name;
    std::string name = type.name();   // MSVC's "class X"
    for (const char* prefix : {"class ", "struct "})
        if (name.rfind(prefix, 0) == 0) name.erase(0, std::strlen(prefix));
    return name;
}

bool IsWindowName(const std::string& name) {
    for (const Named& named : NamedWindows())
        if (name == named.name) return true;
    return false;
}

} // namespace ei::verify::image
