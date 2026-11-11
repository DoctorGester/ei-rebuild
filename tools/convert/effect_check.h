#pragma once

// ei-convert --effect-check (docs/notes/effects.md, "Checks"): particle sources of the original types run on a small
// world of their own (a carrier stone, scripted moves, detaches, stops and rescales), every tick's particles (position,
// size, colour, frame, their previous values, the dead flag), the bounds, the source's flags and the particle
// generator's draws hashed, and the hashes compared with the reference recorded while the type still ran its C++
// emitter (tests/effects/reference.txt, built into the tool). A type moves to data only once its cases match. No game
// data.

#include <string>

namespace effect_check {

// The exit code: 0 when every case matches the reference. `record` given: the reference written there instead (each
// case's output as this build gives it); `dump` given: every tick's particles written there as text, for a diff of two
// builds' runs.
int Main(const std::string& record, const std::string& dump);

}   // namespace effect_check
