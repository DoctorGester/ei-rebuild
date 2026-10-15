// Check for the generated static initialisers (src/standalone/static_init.cpp, S3): the standalone image
// runs them instead of game.exe's 748. In the hook build the originals have run before the hooks go in
// (the entry of WinMain), so this start-up check evaluates the generated stores, copies, fills and float
// folds (Check visitor: the last write to an address wins) and compares each with the original's memory,
// and checks that every once-only guard bit is set. Calls and atexit registrations are not run here:
// tools/check_static_init.py (build.cmd) checks their targets and arguments against game.exe.
//
// Compares against the original. Every game start reaches it; no scenario is needed.
#include "verify.h"

#include "../../src/hook/log.h"
#include "../../src/standalone/static_init.h"

#include <cstdint>
#include <cstring>
#include <map>

namespace {

// Initialisers whose stores a later initialiser's call overwrites before WinMain (the Check visitor
// does not run calls): their values are not compared here; the named check covers what the call built.
struct Rebuilt {
    std::uintptr_t initialiser;
    const char* by;
};
constexpr Rebuilt kRebuilt[] = {
    {0x004E3AD0, "004E3B20 fills the zeroed weather noise tables (WeatherNoiseStartupCheck)"},
    {0x005BF810, "005BF8E0 fills the sector spiral's offsets array (SectorSpiralStartupCheck)"},
    {0x005BF840, "005BF8E0 fills the sector spiral's radius tree (SectorSpiralStartupCheck)"},
};

const char* RebuiltBy(std::uintptr_t initialiser) {
    for (const Rebuilt& r : kRebuilt)
        if (r.initialiser == initialiser) return r.by;
    return nullptr;
}

void StaticInitialisersStartupCheck() {
    ei::static_init::Check check;
    ei::static_init::StaticInitialisers(check);
    // One comparison per initialiser and kind: the number of its values that differ and the first.
    struct Wrong {
        int n = 0;
        std::uintptr_t first = 0;
        std::uint32_t theirs = 0, ours = 0;
    };
    std::map<std::uintptr_t, Wrong> stores, floats;
    int storeCount = 0, floatCount = 0, inexact = 0, unknown = 0, rebuilt = 0;
    for (const auto& [address, v] : check.values) {
        if (!v.known) {
            ++unknown;
            continue;
        }
        if (RebuiltBy(v.initialiser)) {
            ++rebuilt;
            continue;
        }
        const std::uint32_t actual = *reinterpret_cast<const std::uint32_t*>(address);
        bool same = actual == v.bits;
        if (v.isFloat) {
            ++floatCount;
            float expected, got;
            std::memcpy(&expected, &v.bits, 4);
            std::memcpy(&got, &actual, 4);
            if (!same) ++inexact;
            same = ei::verify::NearlyEqual(expected, got, 4, 1e-6f);
        } else {
            ++storeCount;
        }
        Wrong& w = (v.isFloat ? floats : stores)[v.initialiser];
        if (!same && w.n++ == 0) {
            w.first = address;
            w.theirs = actual;
            w.ours = v.bits;
        }
    }
    for (const auto* kind : {&stores, &floats})
        for (const auto& [initialiser, w] : *kind) {
            if (w.n)
                ei::Log("static initialisers: %08X: %d value(s) differ, the first at %08X: %08X / ours %08X",
                        static_cast<unsigned>(initialiser), w.n, static_cast<unsigned>(w.first), w.theirs, w.ours);
            ei::verify::Check(kind == &stores ? "static initialisers: stores" : "static initialisers: float folds", 0, w.n,
                              "initialiser %08X: %d value(s) differ, the first at %08X: %08X / ours %08X",
                              static_cast<unsigned>(initialiser), w.n, static_cast<unsigned>(w.first), w.theirs, w.ours);
        }
    for (const auto& g : check.guards) {
        const std::uint8_t flags = *reinterpret_cast<const std::uint8_t*>(g.flag);
        ei::verify::Check("static initialisers: guards", true, (flags & g.bit) != 0, "%08X bit %u (initialiser %08X) is clear",
                          static_cast<unsigned>(g.flag), g.bit, static_cast<unsigned>(g.initialiser));
    }
    for (const Rebuilt& r : kRebuilt)
        ei::Log("static initialisers: %08X not compared: %s", static_cast<unsigned>(r.initialiser), r.by);
    ei::Log("static initialisers: %d store(s), %d float fold(s) (%d not bit-exact), %zu guard(s) compared; %d value(s) "
            "rebuilt later and %d call result(s) not compared",
            storeCount, floatCount, inexact, check.guards.size(), rebuilt, unknown);
}
EI_STARTUP_CHECK(StaticInitialisersStartupCheck);

} // namespace
