#pragma once

// Starter settings answered for a harness run (verify build only; harness_starter.cpp): the screen
// resolution and Rebirth's WideScreen switch, without writing the user's registry.
//
// Scenario lines (read when the scenario loads, before WinMain):
//   resolution <W>x<H> | desktop   the game's VideoResolution and addon.dll's VIDEORESOLUTION
//                                  ("desktop": the primary display's current mode)
//   widescreen 0|1                 the starter's Settings\WideScreen (addon.dll's switch, and the
//                                  rebuild's for its WideScreen flag)
//   fullscreen 0|1                 the game's FullScreen
//   music <volume>                 the game's AudioStreaming (the streamed music's volume, 0..100); 0: no
//                                  streamed music. The stream thread plays in real time, and the music
//                                  system's random draws follow the track it reports (SV5d5: a fight's
//                                  combat track is picked only when the playing one is not it), so a run
//                                  whose fights must draw the same everywhere plays without it
//   addonpath <folder>             the starter's Settings\AddonPath (the selected Rebirth mod: addon.dll's,
//                                  and the standalone image's mod_folders.h) as harness_saves\<folder>;
//                                  a "# prepare:" tool writes the mod there (tools/make_test_mod.py)
// Without them the environment variables EI_RESOLUTION and EI_WIDESCREEN (same values) apply, and
// without those the registry snapshot's values (harness_settings.h), but for the resolution: a window (FullScreen 0
// unless a "fullscreen" line says otherwise; H31) of 960x720 on the vanilla data and 1280x720 on Rebirth's (the shape
// of the goldens' interface layout). Rebirth's CtfMon switch is answered 0 in every run (S3e: addon.dll closed
// ctfmon at start and restarted it from its DLL_PROCESS_DETACH, which hung the exit).
//
// The answers go into the run's settings file (harness_settings.cpp), by value name over the snapshot's. Our renderer
// changes no display mode.

#include <string>

namespace ei::verify::starter {

// The snapshot's value types (the registry's REG_SZ, REG_BINARY, REG_DWORD).
inline constexpr unsigned long kString = 1, kBinary = 3, kDword = 4;

// The run's answer for the snapshot's value `name` (any key): its type, data and size (`dword` holds a
// 4-byte answer); false when the run leaves it to the snapshot.
bool Answer(const char* name, unsigned long* type, const unsigned char** data, unsigned long* size, unsigned long* dword);

// Consumes a "resolution", "widescreen", "fullscreen", "music" or "addonpath" scenario line; false for any
// other verb.
bool ParseStep(const std::string& verb, const std::string& argument);

// Applies the environment fallbacks (once, at harness start, before harness_settings' Install; `savesDir`:
// the run's harness_saves).
void Install(const char* savesDir);

// The step "modfile <archive> <file> <bytes>": the game's archive <archive> in the text resources' folder opened
// as the game opens it and <file> read through its GetFile (which looks in the mod's archives first, as Rebirth's
// ResOverride did): its size must be <bytes> (0: not found; "!<n>": anything but <n>; "+": found). An error message,
// empty when it matches.
std::string ModFileStep(const std::string& argument);
// "modlist <archive> <pattern> <count>": the same archive's ListFiles(pattern) gives that many names (the mod's
// archives' and the archive's own, none of the archive's when the mod ignores it).
std::string ModListStep(const std::string& argument);
// "modarchive figures|links|textures <file> <bytes>": the size of <file> in the figure manager's, the link figure
// loader's or the texture manager's archives, in their order (the mod's extra archives among them); needs a game.
std::string ModArchiveStep(const std::string& argument);
// "playstream <name>": the music stream is asked for <name> (an .mp3 of the stream folder, with its extension), as the main
// menu's music is; a real-time thread opens it, so a scenario waits for it with "waitread" (harness_modtrace.h) at the main
// menu, where no world update counts the passes the wait takes.
std::string PlayStreamStep(const std::string& argument);
// "mapfile exists <name> yes|no" / "mapfile write <name>": the maps folder's MapFileExists, and WriteMapFile with five
// bytes, as the client's shared-file handler calls them (the folder follows Rebirth's Maps: the mod's or the game's).
std::string MapFileStep(const std::string& argument);

// The mod folder the scenario's "addonpath" step names, in full (empty without one).
std::string AddonPath();

// Whether this run plays the Rebirth install: the image runs with Rebirth flags (rebirth_profile::SettingsOnly; S19).
// Scenario lines "vanilla ..." / "rebirth ..." and the digest's and the UI census's install follow it.

bool RebirthInstall();

// Whether the run's data folder (EI_GAME_ROOT, else the exe's own) is a Rebirth install (starter\addon.dll or
// EI_Plugin.asi, as the image's start-up decides its profile). Known before the game starts, unlike RebirthInstall:
// the image's window size (960x720 on the vanilla data, 1280x720 on Rebirth's) and the scenario lines "vanilla
// resolution ..." / "rebirth resolution ..." follow it.
bool RebirthData();

// The run's game folder in full (EI_GAME_ROOT, else the exe's own folder) in the game's form (backslashes), "" when
// unknown: the install the run reads and never writes.
std::string GameFolder();

} // namespace ei::verify::starter
