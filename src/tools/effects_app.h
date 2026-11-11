#pragma once

// Where the scenario harness of ei-effects (tests/verify/effects; the verify build only) joins its main loop
// (tools/effects_main.cpp). Empty in the product: the program then runs as without it.

#include <string>

namespace tools::effects_app {

struct HarnessHooks {
    // At the start, before the mod is chosen and the window made: the scenario read; `modFolder` set when it names a
    // mod.
    void (*start)(std::string* modFolder) = nullptr;
    // The window made, before it is first shown: the harness keeps it out of sight.
    void (*windowMade)(void* window) = nullptr;
    // The frame drawn, before the present: a capture reads it.
    void (*beforePresent)() = nullptr;
    // After the present: the scenario's next steps (its quit ends the loop: platform::PostQuit).
    void (*afterFrame)() = nullptr;
};

inline HarnessHooks g_harnessHooks;

} // namespace tools::effects_app
