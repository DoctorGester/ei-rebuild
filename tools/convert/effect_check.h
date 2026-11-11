#pragma once

// ei-convert --effect-check (docs/notes/effects.md, "Checks"): particle sources of the original types run on a small
// world of their own (a carrier stone, scripted moves, detaches, stops and rescales), every tick's particles (position,
// size, colour, frame, their previous values, the dead flag), the bounds, the source's flags and the particle
// generator's draws hashed, and the hashes compared with the reference (tests/effects/reference.txt, built into the
// tool; first recorded from the types' C++ emitters, recorded again where an effect changes on purpose). The coarse
// compare (`--coarse`, and after the exact one) runs each case on 8 other seeds and compares how it looks with
// tests/effects/coarse.txt: per quarter of its ticks the live particles, their mean size and colour and the mean and
// spread of their places, and the particles made, dead and their age at death, each within a tolerance. No game data.

#include <string>

namespace effect_check {

struct Options {
    std::string record;         // the reference written there instead (each case's output as this build gives it)
    std::string dump;           // every tick's particles written there as text, for a diff of two builds' runs
    bool coarse = false;        // the coarse compare alone (else the exact one, then the coarse one)
    std::string recordCoarse;   // the coarse reference written there
};

// The exit code: 0 when every case matches the reference (or, coarse, lies within the tolerance of it).
int Main(const Options& options);

}   // namespace effect_check
