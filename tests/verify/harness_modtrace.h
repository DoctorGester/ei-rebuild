#pragma once

// The files a run's game reads, for the mod-folder checks (verify build; S19d; docs/notes/standalone.md, "Mod folders").
// A scenario that uses one of the steps below has every path the game's file calls map (the harness's MapPath: reads,
// listings and asks whether a file exists) noted, once each; the steps then ask where the game looked.
//
// Steps (<where> is "mod": below the run's mod folder, the harness step "mod"'s; or "game": below the game folder
// or its copy in the run's overlay; <path> is relative to that folder):
//   read <where> <path> yes|no           require that the game has (yes) or has not (no) touched that file so far
//   wrote <where> <path> yes|no          the same for a file the game wrote (the path of a write in the run's overlay)
//   waitread <seconds> <where> <path>    wait until it has, <seconds> of real time at most (for a file a thread opens:
//                                        the music stream's)

#include <string>

namespace ei::verify::modtrace {

// A scenario step uses the trace: from then on reads are noted (called while the scenario loads).
void Enable();
bool Enabled();

// Where the folders are: the mod's (empty without a "mod" line), the game folder, the run's overlay (harness_saves).
void SetRoots(const std::string& mod, const std::string& game, const std::string& overlay);

// The game opened, listed or asked about `mapped`, or wrote it (any thread).
void NoteRead(const std::string& mapped);
void NoteWrite(const std::string& mapped);

// Whether `where`'s `path` has been noted; false and an error text for a `where` that is none.
bool Seen(const std::string& where, const std::string& path, bool written, std::string* error);

// The step "read" or "wrote" (`verb`) with its argument: an error message, empty when it holds.
std::string ReadStep(const std::string& verb, const std::string& argument);

// "waitread": the parts of the argument after the seconds; false when it does not parse.
bool ParseWaitRead(const std::string& argument, unsigned long* seconds, std::string* where, std::string* path);

} // namespace ei::verify::modtrace
