#include "game/app.h"
#include "game/settings.h"
#include "hook/log.h"
#include "platform/platform.h"
#include "render/render.h"
#include "standalone/static_init.h"

#include "lib/text_case.h"

#include "ei/hook.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {
std::string g_root;
void RedirectPath(const char* name, std::string& value) {
    if (text::CompareIgnoringCase(name, "Work path") == 0 || text::CompareIgnoringCase(name, "CD-ROM Path") == 0)
        value = g_root;
}
}
int main(int argc, char** argv) {
    bool check = false;
    std::string arguments;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--check") check = true;
        else if (arg == "--") {
            for (++i; i < argc; ++i) {
                if (!arguments.empty()) arguments += ' ';
                arguments += argv[i];
            }
            break;
        } else {
            std::fprintf(stderr, "usage: ei-rebuild [--check] [-- <game arguments>]\n");
            return 2;
        }
    }
    g_root = platform::EnvironmentVariable("EI_GAME_ROOT");
    if (g_root.empty()) g_root = std::filesystem::path(platform::ProgramPath()).parent_path().string();
    std::uint32_t error = 0;
    if (!platform::SetCurrentFolder(g_root, &error)) {
        std::fprintf(stderr, "Cannot enter game folder %s (error %u)\n", g_root.c_str(), error);
        return 1;
    }
    if (g_root.back() != '/' && g_root.back() != '\\') g_root += '/';
    settings::AddReadFilter(RedirectPath);
    const std::string settingsFile = platform::EnvironmentVariable("EI_SETTINGS_PATH");
    if (!settingsFile.empty()) settings::Pin(settingsFile.c_str(), nullptr, false);
    ei::Log("standalone: native startup, game folder %s", g_root.c_str());
    ei::RunStartupActions();
    ei::static_init::RunStaticInitialisers();
    if (check) {
        std::puts("ei-rebuild: native startup check passed");
        return 0;
    }
    render::PrepareDevice();
    return app::GameMain(nullptr, nullptr, arguments.data(), 1);
}
