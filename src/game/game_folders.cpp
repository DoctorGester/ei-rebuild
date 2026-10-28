// The registry key of the options and the game's folders: five CStrings, each built empty by its static
// initialiser in the list's order and destroyed once by the exit function it registers (a guard bit each).
// The initialisers and exit functions are registered under their names (docs/notes/standalone.md, "The static
// initialisers' objects").
#include "game_singletons.h"

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

void __cdecl DestroyRegistryRoot() { DestroyOnce(g_registryRoot, 1); }
void __cdecl DestroyStartFolder() { DestroyOnce(g_startFolder, 2); }
void __cdecl DestroyDataFolder() { DestroyOnce(g_dataFolder, 4); }
void __cdecl DestroyTextFolder() { DestroyOnce(g_textFolder, 8); }
void __cdecl DestroyDiscFolder() { DestroyOnce(g_discFolder, 0x10); }

} // namespace

void __cdecl ConstructRegistryRoot() {
    g_registryRoot.Construct();
    libcrt::AtExit(DestroyRegistryRoot);
}
void __cdecl ConstructStartFolder() {
    g_startFolder.Construct();
    libcrt::AtExit(DestroyStartFolder);
}
void __cdecl ConstructDataFolder() {
    g_dataFolder.Construct();
    libcrt::AtExit(DestroyDataFolder);
}
void __cdecl ConstructTextFolder() {
    g_textFolder.Construct();
    libcrt::AtExit(DestroyTextFolder);
}
void __cdecl ConstructDiscFolder() {
    g_discFolder.Construct();
    libcrt::AtExit(DestroyDiscFolder);
}
