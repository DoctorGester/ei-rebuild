// The registry key of the options and the game's folders (game_singletons.h): five CStrings, each built empty by its
// static initialiser (0055E760-0055E860) in the list's order and destroyed once by the exit function it registers
// (a guard bit each in 007AF438). The initialisers and exit functions are ported on the names and registered at their
// addresses (docs/notes/standalone.md, "The static initialisers' objects").
#include "game_singletons.h"

#include "ei/hook.h"
#include "originals_library.h"

#include <cstdint>

constinit lib::StaticObject<mfc::CString> g_registryRoot = /*007AF450*/ {};
constinit lib::StaticObject<mfc::CString> g_startFolder = /*007AF454*/ {};
constinit lib::StaticObject<mfc::CString> g_discFolder = /*007AF458*/ {};
constinit lib::StaticObject<mfc::CString> g_textFolder = /*007AF45C*/ {};
constinit lib::StaticObject<mfc::CString> g_dataFolder = /*007AF460*/ {};

namespace {

// The exit functions' guard bits: a string already destroyed is not destroyed again.
constinit std::uint8_t g_destroyed = /*007AF438*/ 0;

void DestroyOnce(lib::StaticObject<mfc::CString>& folder, std::uint8_t bit) {
    if (g_destroyed & bit) return;
    g_destroyed = static_cast<std::uint8_t>(g_destroyed | bit);
    folder.Destroy();
}

void __cdecl DestroyRegistryRoot() { DestroyOnce(g_registryRoot, 1); }   // 0055E780
void __cdecl DestroyStartFolder() { DestroyOnce(g_startFolder, 2); }     // 0055E7C0
void __cdecl DestroyDataFolder() { DestroyOnce(g_dataFolder, 4); }       // 0055E800
void __cdecl DestroyTextFolder() { DestroyOnce(g_textFolder, 8); }       // 0055E840
void __cdecl DestroyDiscFolder() { DestroyOnce(g_discFolder, 0x10); }    // 0055E880

using AtExitFunction = void(__cdecl*)();

void __cdecl ConstructRegistryRoot() {   // 0055E760
    g_registryRoot.Construct();
    GameAtExit(ei::Code<AtExitFunction>(0x0055E780));
}
void __cdecl ConstructStartFolder() {   // 0055E7A0
    g_startFolder.Construct();
    GameAtExit(ei::Code<AtExitFunction>(0x0055E7C0));
}
void __cdecl ConstructDataFolder() {   // 0055E7E0
    g_dataFolder.Construct();
    GameAtExit(ei::Code<AtExitFunction>(0x0055E800));
}
void __cdecl ConstructTextFolder() {   // 0055E820
    g_textFolder.Construct();
    GameAtExit(ei::Code<AtExitFunction>(0x0055E840));
}
void __cdecl ConstructDiscFolder() {   // 0055E860
    g_discFolder.Construct();
    GameAtExit(ei::Code<AtExitFunction>(0x0055E880));
}

} // namespace

EI_REPLACE(0x0055E760, ConstructRegistryRoot);
EI_REPLACE(0x0055E7A0, ConstructStartFolder);
EI_REPLACE(0x0055E7E0, ConstructDataFolder);
EI_REPLACE(0x0055E820, ConstructTextFolder);
EI_REPLACE(0x0055E860, ConstructDiscFolder);
EI_REPLACE(0x0055E780, DestroyRegistryRoot);
EI_REPLACE(0x0055E7C0, DestroyStartFolder);
EI_REPLACE(0x0055E800, DestroyDataFolder);
EI_REPLACE(0x0055E840, DestroyTextFolder);
EI_REPLACE(0x0055E880, DestroyDiscFolder);
