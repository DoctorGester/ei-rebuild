#pragma once

// The game settings a harness run reads (verify build only; harness_settings.cpp, WB16): never the user's.
//
// Registry: every key under HKEY_CURRENT_USER or HKEY_LOCAL_MACHINE \Software\Nival Interactive and
// \Software\Gipat.Ru (the original's settings, Rebirth's game settings and starter switches) is answered from
// config\harness\registry.reg, the committed snapshot, through advapi32's own import slots (its exports are
// stubs that jump through them), so the game, the image, ei_hook.dll's own reads and addon.dll all see it.
// Such keys are never opened, created or written in the real registry: opens and creates get handles of
// this module's, writes and deletes on them are dropped (as game.exe's RegSetValueExA always is,
// harness.cpp), and game.exe's import of RegCreateKeyExA never reaches the registry at all. A value or key
// the snapshot lacks fails the run (an entry "name"=- / [-key] marks one read but absent on purpose), as
// does a call on these keys the module does not answer (enumeration, RegGetValue). The scenario's starter
// lines (harness_starter.h) are answered first.
//
// Install files the user's play rewrites and every run reads (config\keyboard.ini: the key bindings,
// config\addrbook.ini: the multiplayer address book): read from config\harness\<vanilla|rebirth>\ unless the
// run's overlay has its own copy (a "# prepare:" tool's, or the game's own write; harness.cpp ReadPath).
//
// The folder: EI_HARNESS_SETTINGS, else config\harness two levels above ei_hook.dll (build\<config>\).

#include <string>

namespace ei::verify::settings {

// Loads the snapshot and answers the registry (once, at harness start, before the game reads its settings).
void Install();

// The pinned copy of `relative` (a path under the game folder, e.g. "config\keyboard.ini") for this run's
// install, written to `out`; false when the file is not pinned.
bool PinnedFile(const char* relative, char (&out)[260]);

// The run's settings problems (values or keys missing from the snapshot, unanswered calls), "; "-separated;
// empty when there were none.
std::string Failure();

} // namespace ei::verify::settings
