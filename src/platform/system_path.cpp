// The game's builds ask the system for every path as the game names it (paths.h). The harness's builds leave this file
// out and map the paths onto their overlay (tests/verify/harness.cpp, SystemPath; CMakeLists.txt, EI_GAME_CALLS).
#include "platform/paths.h"

namespace platform {

std::string SystemPath(std::string_view path, PathUse) { return std::string(path); }

} // namespace platform
