// 005ACE80: the main menu's and the escape menu's button rectangles from "menus.reg" (each entry
// four integers; a missing or malformed one is a fatal error). See docs/notes/world.md.
#include "base_manager.h"
#include "game_strings.h"

#include "ei/hook.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "originals_library.h"

#include <cstdint>

namespace {



// The entries in the order the original reads them: section, name, error, offset of the RECT.
struct MenuRect {
    std::uintptr_t section, name, error;
    int offset;
};
constexpr MenuRect kRects[] = {
    {0x007904EC, 0x00790F9C, 0x00790F7C, 0x00},   // MainMenu NewGame
    {0x007904EC, 0x00790F70, 0x00790F50, 0x10},   // MainMenu LoadGame
    {0x007904EC, 0x00790F44, 0x00790F24, 0x20},   // MainMenu ExitGame
    {0x007904EC, 0x00790F1C, 0x00790EFC, 0x30},   // MainMenu Options
    {0x007904EC, 0x00790EF4, 0x00790ED4, 0x40},   // MainMenu Credits
    {0x007904EC, 0x00790EC8, 0x00790EA4, 0x50},   // MainMenu Multiplayer
    {0x00790E90, 0x00790E98, 0x00790E70, 0x60},   // EscMenu SaveGame
    {0x00790E90, 0x00790F70, 0x00790E50, 0x70},   // EscMenu LoadGame
    {0x00790E90, 0x00790F1C, 0x00790E30, 0xA0},   // EscMenu Options
    {0x00790E90, 0x00790E24, 0x00790E00, 0x80},   // EscMenu ResumeGame
    {0x00790E90, 0x00790DF8, 0x00790DD8, 0x90},   // EscMenu ExitMM
};

// The .reg reader (0x18 bytes on the stack): 00694520 constructor (RegistryOriginals::Construct), 00694610 (RET 4) parse a file (RegistryOriginals::Attach),
// 00694540 destructor (RegistryOriginals::Destroy), 00695410 (RET 0xC) read a section's integer list.
RegistryOriginals* AsRegistry(void* reg) { return static_cast<RegistryOriginals*>(reg); }
__declspec(naked) void __fastcall RegReadInts(void*, void*, const char*, const char*, void*) EI_ORIGINAL(0x00695410)
// 00480080. game::FatalError(message); returns what the original returns to the caller.

// An aligned array of ints (data, size, capacity, growBy); the block pointer is kept at data[-1].
using IntArray = lib::AlignedArray<int>;

using SetRectFn = int(__stdcall*)(void*, int, int, int, int);
SetRectFn ImportSetRect() { return *reinterpret_cast<SetRectFn const*>(0x0073B478); }

} // namespace

int __fastcall LoadMenuRects(void* owner, void*, std::uint8_t* rects) {
    alignas(4) std::uint8_t reg[0x18];
    AsRegistry(reg)->Construct();
    // The owning manager's GetFile (slot 19): the resource file's data.
    void* file = static_cast<CBaseManager*>(owner)->GetFile(game::menutexts::MenusReg(), nullptr);
    if (!file || !AsRegistry(reg)->Attach(file)) {
        int result = game::FatalError(file ? game::menutexts::CantParse() : game::menutexts::CantOpen());
        AsRegistry(reg)->Destroy();
        return result;
    }
    IntArray values{nullptr, 0, 0, 0};
    SetRectFn setRect = ImportSetRect();
    for (const MenuRect& entry : kRects) {
        DwordsSetSize(reinterpret_cast<lib::AlignedArray<std::uint32_t>*>(&values), nullptr, 0, -1);
        RegReadInts(reg, nullptr, game::Text(entry.section), game::Text(entry.name), &values);
        if (values.size != 4) {
            int result = game::FatalError(game::Text(entry.error));
            if (values.data) gamecrt::operator_delete(reinterpret_cast<void**>(values.data)[-1]);
            AsRegistry(reg)->Destroy();
            return result;
        }
        setRect(rects + entry.offset, values.data[0], values.data[1], values.data[2], values.data[3]);
    }
    if (values.data) gamecrt::operator_delete(reinterpret_cast<void**>(values.data)[-1]);
    AsRegistry(reg)->Destroy();
    return 1;
}

EI_REPLACE(0x005ACE80, LoadMenuRects);
