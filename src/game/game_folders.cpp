// The registry key of the options and the game's folders. The disc's and the data folder are built empty by their
// static initialisers in the list's order and destroyed once by the exit functions they register (a guard bit each),
// defined on their names (docs/notes/standalone.md, "The static initialisers' objects"), while the interface's files
// spell them as static objects; the other three are plain globals.
#include "game_singletons.h"

#include <cstdint>
#include <cstdlib>
#include <string>

std::string g_registryRoot;
std::string g_startFolder;
std::string g_textFolder;
constinit lib::StaticObject<std::string> g_discFolder = /*007AF458*/ {};
constinit lib::StaticObject<std::string> g_dataFolder = /*007AF460*/ {};

namespace {

// The exit functions' guard bits: a string already destroyed is not destroyed again.
constinit std::uint8_t g_destroyed = /*007AF438*/ 0;

void DestroyOnce(lib::StaticObject<std::string>& folder, std::uint8_t bit) {
    if (g_destroyed & bit) return;
    g_destroyed = static_cast<std::uint8_t>(g_destroyed | bit);
    folder.Destroy();
}

void DestroyDataFolder() { DestroyOnce(g_dataFolder, 4); }
void DestroyDiscFolder() { DestroyOnce(g_discFolder, 0x10); }

} // namespace

void ConstructDataFolder() {
    g_dataFolder.Construct();
    std::atexit(DestroyDataFolder);
}
void ConstructDiscFolder() {
    g_discFolder.Construct();
    std::atexit(DestroyDiscFolder);
}
