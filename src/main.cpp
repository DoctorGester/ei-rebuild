// ei-rebuild: the game.
// usage: ei-rebuild [--effects [<effect>] [--zone <zone>]] [-- <game arguments>]
//   --effects starts the effect viewer (src/tools/effect_viewer.h; docs/modding.md, "The effect viewer") straight in a
//   new game, with no start movies and no main menu: in <zone> (a battle zone of the map list) when given, else the
//   campaign's first; <effect> picked once the world is there.
//   The game folder is the one EI_GAME_ROOT names (tools/run_all.py's), else the platform's
//   (platform::DefaultGameFolder: the exe's own, dropped into a game folder; on macOS beside the
//   app or chosen); without one (none chosen) the program ends at once. With EI_GAME_ROOT the
//   settings file's Work path is read as that folder. What follows "--" is the game's command
//   line, as typed (WinMain's).
#include "game/app/app.h"
#include "game/data/settings.h"
#include "game/app/tool_hooks.h"
#include "lib/text_case.h"
#include "platform/log.h"
#include "platform/platform.h"
#include "tools/effect_viewer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

std::string g_gameRoot;   // EI_GAME_ROOT with a trailing separator

void RedirectGamePath(const char* name, std::string& value) {
    if (text::CompareIgnoringCase(name, "Work path") == 0) value = g_gameRoot;
}

std::string GameFolder() {
    std::string folder = platform::EnvironmentVariable("EI_GAME_ROOT");
    if (folder.empty()) folder = platform::DefaultGameFolder();
    while (folder.size() > 3 && (folder.back() == '\\' || folder.back() == '/')) folder.pop_back();
    return folder;
}

} // namespace

int main(int argc, char** argv) {
    // Under run_all stdout is a file: unbuffered, so a run that ends abruptly loses none of it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int next = 1;
    bool effects = false;
    std::string effect;
    if (next < argc && std::strcmp(argv[next], "--effects") == 0) {
        effects = true;
        ++next;
        if (next < argc && argv[next][0] != '-') effect = argv[next++];
        if (next + 1 < argc && std::strcmp(argv[next], "--zone") == 0) {
            app::g_toolScene.zone = argv[next + 1];
            next += 2;
        }
    }
    if (next < argc && std::strcmp(argv[next], "--") != 0) {
        std::fprintf(stderr, "usage: ei-rebuild [--effects [<effect>] [--zone <zone>]] [-- <game arguments>]\n");
        return 2;
    }
    const std::string folder = GameFolder();
    if (folder.empty()) {
        ei::Log("ei-rebuild: no game folder");
        return 1;
    }
    std::uint32_t error = 0;
    if (!platform::SetCurrentFolder(folder, &error))
        ei::Log("cannot enter the game folder %s (error %u)", folder, error);
    if (!platform::EnvironmentVariable("EI_GAME_ROOT").empty()) {
        g_gameRoot = folder + "\\";
        settings::AddReadFilter(&RedirectGamePath);
    }
    const std::string settingsFile = platform::EnvironmentVariable("EI_SETTINGS_PATH");
    if (!settingsFile.empty()) settings::Pin(settingsFile, nullptr, false);
    std::string arguments = platform::ArgumentsAfterSeparator(argc, argv);
    std::atexit([] { ei::Log("exit: the exit functions done"); });
    ei::Log("ei-rebuild started in %s, arguments '%s'", folder, arguments);
    // The one-instance check, SDL's video and the renderer's device (~290 ms on a thread of its own): GameMain's
    // start-up runs beside it.
    app::StartVideo();
    // The settings' [game] Mod: its files then stand over the game folder's and its saves keep apart.
    app::UseMod(folder, settings::GetString(settings::kGame, "Mod"));
    if (effects) {
        app::g_toolScene.newGame = true;
        tools::effect_viewer::Open(effect);
    }
    const int result = app::GameMain(arguments.data());
    ei::Log("exit: the game's main returned");
    return result;
}
