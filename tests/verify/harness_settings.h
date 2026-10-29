#pragma once

// The game settings a harness run reads (verify build only; harness_settings.cpp, WB16): never the user's.
//
// Registry: every key under HKEY_CURRENT_USER or HKEY_LOCAL_MACHINE \Software\Nival Interactive and
// \Software\Gipat.Ru (the original's settings, Rebirth's game settings and starter switches) is answered from
// config\harness\registry.reg, the committed snapshot, through advapi32's own import slots (its exports are
// stubs that jump through them), so the game, the image, the harness's own reads and addon.dll all see it.
// Such keys are never opened, created or written in the real registry: opens and creates get handles of
// this module's, writes and deletes on them are dropped (as game.exe's RegSetValueExA always is,
// harness.cpp), and game.exe's import of RegCreateKeyExA never reaches the registry at all. A value or key
// the snapshot lacks fails the run (an entry "name"=- / [-key] marks one read but absent on purpose), as
// does a call on these keys the module does not answer (enumeration, RegGetValue). The scenario's starter
// lines (harness_starter.h) are answered first.
//
// The standalone image keeps its settings in a file instead (game/settings_file.h, ST1): its runs get their own,
// <run folder>\ei_settings.ini, written at start from the snapshot's HKEY_CURRENT_USER keys with the starter
// lines' answers in place (any key with that value name) and pinned: read only, and a read of a value the file
// lacks fails the run as above (unless marked absent). The game paths'
// redirects (hook/paths.cpp, harness_startup.cpp's discprompt) change its reads as they did the registry's.
//
// Two scenario lines (ST1a) check the product's file paths instead, in the image only:
//   settingsfile writable   the run's file is not read only: it also holds a probe key of odd values (every
//                           value type, escapes, NaN and -0 floats), and the game's exit save (WinMain's
//                           GameOptions::SaveToRegistry, through WinRegistry::Close's Flush) rewrites it,
//                           .tmp then moved over it
//   settingsfile defaults   no file at the start: the first use writes it with the defaults
//                           (settings_file::DefaultDocument, for a 1280x720 desktop: SetDesktopSize), the game
//                           reads every value they leave out as absent, and the exit save rewrites it
// tools/settings_file_check.py compares the run folder's ei_settings.ini after the run with the expected file the
// scenario names in a "# settings-file: <file>" line (tests/scenarios/settings/), and run_all adds the differences
// to the run's failures. The user's registry is never read or written in either.
//
// Install files the user's play rewrites and every run reads (config\keyboard.ini: the key bindings,
// config\addrbook.ini: the multiplayer address book): read from config\harness\<vanilla|rebirth>\ unless the
// run's overlay has its own copy (a "# prepare:" tool's, or the game's own write; harness.cpp ReadPath).
//
// The folder: EI_HARNESS_SETTINGS, else config\harness two levels above our module (build\<config>\).

#include <string>

namespace ei::verify::settings {

// Loads the snapshot and answers the registry (once, at harness start, before the game reads its settings);
// in the standalone image also writes and pins the run's settings file.
void Install();

// Consumes a "settingsfile writable|defaults" scenario line (read when the scenario loads); false for any other verb.
bool ParseStep(const std::string& verb, const std::string& argument);

// The pinned copy of `relative` (a path under the game folder, e.g. "config\keyboard.ini") for this run's
// install, written to `out`; false when the file is not pinned.
bool PinnedFile(const char* relative, char (&out)[260]);

// The run's settings problems (values or keys missing from the snapshot, unanswered calls), "; "-separated;
// empty when there were none.
std::string Failure();

} // namespace ei::verify::settings
