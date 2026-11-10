#pragma once

// Script texts run both ways (docs/notes/lua-scripting.md, "Lockstep"): by the game's executor (Script::Load,
// Script::Tick) and as their converted Lua (script_lua_writer.h) under the game's Lua scheduler
// (script_lua_scheduler.h), every global (floats by their bits; loop counters the Lua does not write left out), the
// instances (by definition), the waiting-run count and the random generator compared after every tick. The Lua side
// loads its texts as the game does (Script::Load, which hands each to the scheduler's runtime conversion), QFinish's
// quest scripts included. Part of ei-convert --lua-check (short texts on lua_check's world of three stones) and of
// --scripts --lockstep (every text of the survey).

#include <cstddef>
#include <string>
#include <vector>

namespace lua_both {

// A text loaded into the script before the tick of its number.
struct Text {
    int tick = 0;
    std::string text;
};

struct Outcome {
    std::string difference;   // the first one (or a Lua error), with its tick; empty when every tick matched
    int ticks = 0;            // compared
    // The game loaded a text of its own (QFinish's quest script; the Lua side's QFinish converts it at once).
    bool questLoaded = false;
    // Not run: a keyword the test world cannot run (PlayMovie, PlayCamera: the game's data or window), named here.
    std::string leftOut;
    std::size_t mostInstances = 0;
    std::size_t mostRuns = 0;
};

// Where the Lua side goes through a save, after the compare of the tick of each number (-1: none): `oldFormat`, the
// game's script saved there (Script::SaveState) and loaded into a script of its own, and the Lua resumed from it by
// the old-save shim (ScriptLuaScheduler::Resume); `eris`, the Lua state saved and restored into a new scheduler. The
// state each gives is compared with the game's of that tick at once, and the ticks after go on with it.
struct Saves {
    int oldFormat = -1;
    int eris = -1;
};

// The texts loaded into one script, run both ways for `ticks` ticks; with `resetTick` >= 0 the zone reset
// (Script::ResetZone, ScriptLuaScheduler::ResetZone) before that tick. A text the game's parser stops in is loaded as
// the game loads it: what came before, its WorldScript not started.
Outcome Compare(const std::vector<Text>& texts, int ticks, int resetTick = -1, Saves saves = {});

// True while Compare runs: the tool's log drops the game's warnings of the test world (no unit prototypes, no
// spells).
extern bool g_logMuted;

// The short texts, each also through a save at every tick (the old format, then Eris a tick later; Eris alone):
// prints each difference; returns the number of failed checks and adds the checks made to *checks.
int Check(int* checks);

}   // namespace lua_both
