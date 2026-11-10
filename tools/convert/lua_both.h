#pragma once

// Script texts run both ways (docs/notes/lua-scripting.md, "Lockstep"): by the game's executor (Script::Load,
// Script::Tick) and as their converted Lua (script_lua_writer.h) under the game's Lua scheduler
// (script_lua_scheduler.h), every global (floats by their bits; loop counters the Lua does not write left out), the
// instance and waiting-run counts and the random generator compared after every tick. Part of ei-convert --lua-check
// (short texts on lua_check's world of three stones) and of --scripts --lockstep (every text of the survey).

#include <cstddef>
#include <string>
#include <vector>

namespace lua_both {

// A text loaded into the script before the tick of its number.
struct Text {
    int tick = 0;
    std::string name;   // the Lua chunk's
    std::string text;
};

struct Outcome {
    std::string difference;   // the first one (or a Lua error), with its tick; empty when every tick matched
    int ticks = 0;            // compared
    // The game loaded a text of its own (QFinish's quest script; L7 runs it in Lua): compared up to that tick.
    bool questLoaded = false;
    // Not run: a keyword the test world cannot run (PlayMovie, PlayCamera: the game's data or window), named here.
    std::string leftOut;
    std::size_t mostInstances = 0;
    std::size_t mostRuns = 0;
};

// The texts loaded into one script, run both ways for `ticks` ticks; with `resetTick` >= 0 the zone reset
// (Script::ResetZone, ScriptLuaScheduler::ResetZone) before that tick. A text the game's parser stops in is loaded as
// the game loads it: what came before, its WorldScript not started.
Outcome Compare(const std::vector<Text>& texts, int ticks, int resetTick = -1);

// True while Compare runs: the tool's log drops the game's warnings of the test world (no unit prototypes, no
// spells).
extern bool g_logMuted;

// The short texts: prints each difference; returns the number of failed checks and adds the checks made to *checks.
int Check(int* checks);

}   // namespace lua_both
