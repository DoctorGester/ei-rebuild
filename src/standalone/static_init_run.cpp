// Running the generated static initialisers in the standalone image, and what the image still lacks
// for them and their exit functions (static_init.h; docs/notes/standalone.md, S3, S3a).
#include "static_init.h"

#include "game/originals_library.h"
#include "hook/log.h"

#include <set>

namespace ei::static_init {

void Run::AtExit(std::uintptr_t fn) { GameAtExit(reinterpret_cast<void(__cdecl*)()>(fn)); }

void RunStaticInitialisers() {
    Run run;
    StaticInitialisers(run);
}

int ReportMissingTargets() {
    Targets targets;
    StaticInitialisers(targets);
    // An address of game.exe's code with no jump written into it is still INT3 in the image.
    auto missing = [](const std::vector<std::uintptr_t>& list, const char* what) {
        std::set<std::uintptr_t> seen;
        int n = 0;
        for (std::uintptr_t a : list) {
            if (!seen.insert(a).second || *reinterpret_cast<const unsigned char*>(a) != 0xCC) continue;
            Log("standalone: static initialisers: %s %08X has no code yet", what, static_cast<unsigned>(a));
            ++n;
        }
        return n;
    };
    const int calls = missing(targets.calls, "call target");
    const int exits = missing(targets.exits, "exit function");
    // What the generated exit functions call (library destructors, S4-S12).
    Targets callees;
    for (std::uintptr_t fn : targets.exits) ExitFunction(callees, fn);
    const int exitCalls = missing(callees.calls, "exit function's callee");
    int imports = 0;
    for (std::uintptr_t slot : targets.imports)
        if (!GameData<const std::uintptr_t>(slot)) ++imports;
    Log("standalone: static initialisers: %zu call(s), %zu exit function(s), %zu import call(s); without code: %d call "
        "target(s), %d exit function(s), %d import(s), %d exit function callee(s)",
        targets.calls.size(), targets.exits.size(), targets.imports.size(), calls, exits, imports, exitCalls);
    return calls + exits + imports + exitCalls;
}

} // namespace ei::static_init
