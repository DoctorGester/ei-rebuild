// The menus' button rectangles (figure_manager.h): read from "menus.reg" through the figure manager.
#include "game/figure_manager.h"

#include "game/data/registry.h"
#include "lib/containers.h"
#include "lib/rect.h"

#include <cstdint>
#include <vector>

// The main menu's and the escape menu's button rectangles from "menus.reg" (each entry
// four integers; a missing or malformed one ends the game). See docs/notes/world.md.

Rect g_menuRects[kMenuRects] = /*007C8048*/ {};

namespace {

// The entries in the order the original reads them: section, name, error, the RECT's index.
struct MenuRect {
    const char* section = nullptr;
    const char* name = nullptr;
    const char* error = nullptr;
    int index = 0;
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

int FigureManager::LoadMenuRects(Rect* rects) {
    FileRegistry reg;
    // The manager's GetFile: the resource file's data.
    void* file = GetFile("menus.reg", nullptr);
    if (!file || !reg.Attach(file)) {
        game::FatalExit("%s", file ? "Can't open \"menus.reg\" as .reg with the main/esc menu rects\n" : "Can't open file \"menus.reg\" with the main/esc menu rects\n");
    }
    std::vector<std::int32_t> values;
    for (const MenuRect& entry : kRects) {
        values.clear();
        reg.GetIntArray(entry.section, entry.name, &values);
        if (std::ssize(values) != 4) game::FatalExit("%s", entry.error);
        rects[entry.index] = {values[0], values[1], values[2], values[3]};
    }
    lib::FreeStorage(values);
    return 1;
}
