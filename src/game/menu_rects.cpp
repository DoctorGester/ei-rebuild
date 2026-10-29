// The main menu's and the escape menu's button rectangles from "menus.reg" (each entry
// four integers; a missing or malformed one is a fatal error). See docs/notes/world.md.
#include "figure_manager.h"
#include "registry.h"
#include "game_strings.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

#include <cstdint>

RECT g_menuRects[kMenuRects] = /*007C8048*/ {};

namespace {

// The entries in the order the original reads them: section, name, error, the RECT's index.
struct MenuRect {
    const char* section;
    const char* name;
    const char* error;
    int index;
};
constexpr MenuRect kRects[] = {
    {"MainMenu", "NewGame", "Can't get rect for MM NewGame\n", 0},
    {"MainMenu", "LoadGame", "Can't get rect for MM LoadGame\n", 1},
    {"MainMenu", "ExitGame", "Can't get rect for MM ExitGame\n", 2},
    {"MainMenu", "Options", "Can't get rect for MM Options\n", 3},
    {"MainMenu", "Credits", "Can't get rect for MM Credits\n", 4},
    {"MainMenu", "Multiplayer", "Can't get rect for MM Multiplayer\n", 5},
    {"EscMenu", "SaveGame", "Can't get rect for EM SaveGame\n", 6},
    {"EscMenu", "LoadGame", "Can't get rect for EM LoadGame\n", 7},
    {"EscMenu", "Options", "Can't get rect for EM Options\n", 10},
    {"EscMenu", "ResumeGame", "Can't get rect for EM ResumeGame\n", 8},
    {"EscMenu", "ExitMM", "Can't get rect for EM ExitMM\n", 9},
};

} // namespace

int CFigureManager::LoadMenuRects(RECT* rects) {
    CFileRegistry reg;
    // The manager's GetFile: the resource file's data.
    void* file = GetFile(game::menutexts::MenusReg(), nullptr);
    if (!file || !reg.Attach(file)) {
        return game::FatalError(file ? game::menutexts::CantParse() : game::menutexts::CantOpen());
    }
    std::vector<std::int32_t> values;
    for (const MenuRect& entry : kRects) {
        values.clear();
        reg.GetIntArray(entry.section, entry.name, &values);
        if (std::ssize(values) != 4) {
            int result = game::FatalError(entry.error);
            lib::FreeStorage(values);
            return result;
        }
        gameapi::SetRect(&rects[entry.index], values[0], values[1], values[2],
                         values[3]);
    }
    lib::FreeStorage(values);
    return 1;
}
