// Running the generated static initialisers in the standalone image, and what the image still lacks
// for them and their exit functions (static_init.h; docs/notes/standalone.md).
#include "static_init.h"

#include "hook/log.h"
#include "lib/crt.h"

#include <set>

namespace ei::static_init {

void Run::AtExit(void(__cdecl* fn)()) { libcrt::AtExit(fn); }
void Run::AtExit(std::uintptr_t fn) { libcrt::AtExit(reinterpret_cast<void(__cdecl*)()>(fn)); }

void RunStaticInitialisers() {
    Run run;
    StaticInitialisers(run);
}

int ReportMissingTargets() {
    Targets targets;
    StaticInitialisers(targets);
    // An address of game.exe's code with no registration has no code in the image (CodeAddress's stub).
    auto missing = [](const std::vector<std::uintptr_t>& list, const char* what) {
        std::set<std::uintptr_t> seen;
        int n = 0;
        for (std::uintptr_t a : list) {
            if (!seen.insert(a).second || HasCode(a)) continue;
            Log("standalone: static initialisers: %s %08X has no code yet", what, static_cast<unsigned>(a));
            ++n;
        }
        return n;
    };
    const int calls = missing(targets.calls, "call target");
    const int exits = missing(targets.exits, "exit function");
    int imports = 0;
    for (const void* slot : targets.imports)
        if (!*static_cast<const void* const*>(slot)) ++imports;
    Log("standalone: static initialisers: %zu call(s) and %zu exit function(s) by address, %zu import call(s); "
        "without code: %d call target(s), %d exit function(s), %d import(s)",
        targets.calls.size(), targets.exits.size(), targets.imports.size(), calls, exits, imports);
    return calls + exits + imports;
}

} // namespace ei::static_init
