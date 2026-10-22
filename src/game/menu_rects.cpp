// 005ACE80: the main menu's and the escape menu's button rectangles from "menus.reg" (each entry
// four integers; a missing or malformed one is a fatal error). See docs/notes/world.md.
#include "figure_manager.h"
#include "registry.h"
#include "game_strings.h"

#include "ei/hook.h"

#include "lib/static_object.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "game_data.h"
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

// 00480080. game::FatalError(message); returns what the original returns to the caller.

} // namespace

int CFigureManager::LoadMenuRects(RECT* rects) {
    // The .reg reader (0x18 bytes on the stack, constructed and destroyed by hand).
    lib::InPlace<CFileRegistry> storage;
    auto* reg = storage.Get();
    reg->Construct();
    // The manager's GetFile (slot 19): the resource file's data.
    void* file = GetFile(game::menutexts::MenusReg(), nullptr);
    if (!file || !reg->Attach(file)) {
        int result = game::FatalError(file ? game::menutexts::CantParse() : game::menutexts::CantOpen());
        reg->Destroy();
        return result;
    }
    IntArray values{nullptr, 0, 0, 0};
    for (const MenuRect& entry : kRects) {
        values.SetSize(0, -1);
        reg->GetIntArray(entry.section, entry.name, &values);
        if (values.size != 4) {
            int result = game::FatalError(entry.error);
            if (values.data) lib::FreeAligned32(values.data);
            reg->Destroy();
            return result;
        }
        gameapi::SetRect(&rects[entry.index], values.data[0], values.data[1], values.data[2],
                         values.data[3]);
    }
    if (values.data) lib::FreeAligned32(values.data);
    reg->Destroy();
    return 1;
}

EI_REWRITTEN(0x005ACE80, CFigureManager::LoadMenuRects);
