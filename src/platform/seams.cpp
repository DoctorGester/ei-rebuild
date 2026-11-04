// The game's answers to the platform's seams (seams.h): every path as the game names it, no write watch, the desktop
// shared. The harness's builds leave this file out and answer them themselves (tests/verify/platform/game_answers.cpp;
// CMakeLists.txt, EI_GAME_CALLS).
#include "platform/seams.h"

namespace platform {

std::string SystemPath(std::string_view path, PathUse) { return std::string(path); }

bool WriteFailsAsFull(std::string_view) { return false; }

void WriteFailed(std::string_view, std::intptr_t, unsigned, unsigned long, bool) {}

bool IsolatedRun() { return false; }

} // namespace platform
