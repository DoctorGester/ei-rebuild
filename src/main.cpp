// ei-rebuild: the game.
// usage: ei-rebuild [-- <game arguments>]
//   The game folder is the one EI_GAME_ROOT names (a launcher's: ei_launch, play.cmd's x64 start), else the program's
//   own (the exe dropped into a game folder). With EI_GAME_ROOT the settings file's game paths (Work path, CD-ROM Path)
//   are read as that folder. What follows "--" is the game's command line, as typed (WinMain's).
#include "game/app.h"
#include "game/settings.h"
#include "hook/log.h"
#include "hook/rebirth_hook.h"
#include "lib/text_case.h"
#include "platform/platform.h"
#include "render/render.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string g_gameRoot;   // EI_GAME_ROOT with a trailing separator

void RedirectGamePath(const char* name, std::string& value) {
    if (text::CompareIgnoringCase(name, "Work path") == 0 || text::CompareIgnoringCase(name, "CD-ROM Path") == 0)
        value = g_gameRoot;
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
    // Under ei_launch stdout is a pipe: unbuffered, so a run that ends abruptly loses none of it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc && std::strcmp(argv[i], "--") != 0; ++i) {
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
    std::string arguments = platform::ArgumentsAfterSeparator(argc, argv);
    ei::Log("ei-rebuild started in %s, arguments '%s'", folder.c_str(), arguments.c_str());
    ei::rebirth_hook::Load(folder);
    render::PrepareDevice();   // the renderer's device made while the game starts up
    return app::GameMain(platform::ProgramInstance(), nullptr, arguments.data(), 1);
}
