// ei-rebuild: the game.
// usage: ei-rebuild [-- <game arguments>]
//   The game folder is the one EI_GAME_ROOT names (tools/run_all.py's, play.cmd's), else the program's
//   own (the exe dropped into a game folder). With EI_GAME_ROOT the settings file's Work path
//   is read as that folder. What follows "--" is the game's command line, as typed (WinMain's).
#include "game/app.h"
#include "game/rebirth_profile.h"
#include "game/settings.h"
#include "lib/text_case.h"
#include "platform/log.h"
#include "platform/platform.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string g_gameRoot;   // EI_GAME_ROOT with a trailing separator

void RedirectGamePath(const char* name, std::string& value) {
    if (text::CompareIgnoringCase(name, "Work path") == 0) value = g_gameRoot;
}

std::string GameFolder() {
    std::string folder = platform::EnvironmentVariable("EI_GAME_ROOT");
    if (folder.empty()) {
        folder = platform::ProgramPath();
        const std::size_t slash = folder.find_last_of("\\/");
        folder.resize(slash == std::string::npos ? 0 : slash);
    }
    while (folder.size() > 3 && (folder.back() == '\\' || folder.back() == '/')) folder.pop_back();
    return folder;
}

} // namespace

int main(int argc, char** argv) {
    // Under run_all stdout is a file: unbuffered, so a run that ends abruptly loses none of it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc > 1 && std::strcmp(argv[1], "--") != 0) {
        std::fprintf(stderr, "usage: ei-rebuild [-- <game arguments>]\n");
        return 2;
    }
    const std::string folder = GameFolder();
    std::uint32_t error = 0;
    if (!folder.empty() && !platform::SetCurrentFolder(folder, &error))
        ei::Log("cannot enter the game folder %s (error %u)", folder.c_str(), error);
    if (!platform::EnvironmentVariable("EI_GAME_ROOT").empty()) {
        g_gameRoot = folder + "\\";
        settings::AddReadFilter(&RedirectGamePath);
    }
    const std::string settingsFile = platform::EnvironmentVariable("EI_SETTINGS_PATH");
    if (!settingsFile.empty()) settings::Pin(settingsFile.c_str(), nullptr, false);
    std::string arguments = platform::ArgumentsAfterSeparator(argc, argv);
    ei::Log("ei-rebuild started in %s, arguments '%s'", folder.c_str(), arguments.c_str());
    ei::rebirth_profile::Load(folder);
    return app::GameMain(platform::ProgramInstance(), nullptr, arguments.data(), 1);
}
