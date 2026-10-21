// 005ACE80: the main menu's and the escape menu's button rectangles from "menus.reg" (each entry
// four integers; a missing or malformed one is a fatal error). See docs/notes/world.md.
#include "base_manager.h"
#include "registry.h"
#include "game_strings.h"

#include "ei/hook.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "game_data.h"

#include <cstdint>

namespace {



// The entries in the order the original reads them: section, name, error, offset of the RECT.
struct MenuRect {
    const char* section;
    const char* name;
    const char* error;
    int offset;
};
constexpr MenuRect kRects[] = {
    {"MainMenu", "NewGame", "Can't get rect for MM NewGame\n", 0x00},
    {"MainMenu", "LoadGame", "Can't get rect for MM LoadGame\n", 0x10},
    {"MainMenu", "ExitGame", "Can't get rect for MM ExitGame\n", 0x20},
    {"MainMenu", "Options", "Can't get rect for MM Options\n", 0x30},
    {"MainMenu", "Credits", "Can't get rect for MM Credits\n", 0x40},
    {"MainMenu", "Multiplayer", "Can't get rect for MM Multiplayer\n", 0x50},
    {"EscMenu", "SaveGame", "Can't get rect for EM SaveGame\n", 0x60},
    {"EscMenu", "LoadGame", "Can't get rect for EM LoadGame\n", 0x70},
    {"EscMenu", "Options", "Can't get rect for EM Options\n", 0xA0},
    {"EscMenu", "ResumeGame", "Can't get rect for EM ResumeGame\n", 0x80},
    {"EscMenu", "ExitMM", "Can't get rect for EM ExitMM\n", 0x90},
};

// 00480080. game::FatalError(message); returns what the original returns to the caller.

// An aligned array of ints (data, size, capacity, growBy); the block pointer is kept at data[-1].
using IntArray = lib::AlignedArray<int>;

using SetRectFn = int(__stdcall*)(void*, int, int, int, int);
SetRectFn ImportSetRect() { return ei::GameData<SetRectFn const>(0x0073B478); }

} // namespace

int __fastcall LoadMenuRects(void* owner, void*, std::uint8_t* rects) {
    // The .reg reader (0x18 bytes on the stack, constructed and destroyed by hand).
    alignas(4) std::uint8_t storage[sizeof(CFileRegistry)];
    auto* reg = reinterpret_cast<CFileRegistry*>(storage);
    reg->Construct();
    // The owning manager's GetFile (slot 19): the resource file's data.
    void* file = static_cast<CBaseManager*>(owner)->GetFile(game::menutexts::MenusReg(), nullptr);
    if (!file || !reg->Attach(file)) {
        int result = game::FatalError(file ? game::menutexts::CantParse() : game::menutexts::CantOpen());
        reg->Destroy();
        return result;
    }
    IntArray values{nullptr, 0, 0, 0};
    SetRectFn setRect = ImportSetRect();
    for (const MenuRect& entry : kRects) {
        reinterpret_cast<lib::AlignedArray<std::uint32_t>*>(&values)->SetSize(0, -1);
        reg->GetIntArray(entry.section, entry.name, reinterpret_cast<lib::AlignedArray<std::uint32_t>*>(&values));
        if (values.size != 4) {
            int result = game::FatalError(entry.error);
            if (values.data) gamecrt::operator_delete(reinterpret_cast<void**>(values.data)[-1]);
            reg->Destroy();
            return result;
        }
        setRect(rects + entry.offset, values.data[0], values.data[1], values.data[2], values.data[3]);
    }
    if (values.data) gamecrt::operator_delete(reinterpret_cast<void**>(values.data)[-1]);
    reg->Destroy();
    return 1;
}

EI_REPLACE(0x005ACE80, LoadMenuRects);
