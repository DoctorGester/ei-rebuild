// The game's answers to the platform's seams (seams.h): every path as the game names it, or with a mod as its layer
// gives it (path_layers.h), no write watch, the desktop shared. The harness's builds leave this file out and answer them
// themselves (tests/verify/platform/game_answers.cpp; CMakeLists.txt, EI_GAME_CALLS).
#include "platform/seams.h"

#include "platform/path_layers.h"

namespace platform {

std::string SystemPath(std::string_view path, PathUse use) {
    const Mod* mod = CurrentMod();
    return mod ? mod->map.Map(path, use) : std::string(path);
}

std::vector<FolderSearch> SystemSearches(std::string_view pattern) {
    const Mod* mod = CurrentMod();
    if (!mod) return {{std::string(pattern), {}}};
    return mod->map.Searches(pattern);
}

bool WriteFailsAsFull(std::string_view) { return false; }

void WriteFailed(std::string_view, std::intptr_t, unsigned, unsigned long, bool) {}

bool IsolatedRun() { return false; }

} // namespace platform
