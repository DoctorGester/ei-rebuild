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
//                                  streamed music. With a device the stream plays in real time, and the music
//                                  system's random draws follow the track it reports (SV5d5: a fight's
//                                  combat track is picked only when the playing one is not it), so a run
//                                  whose fights must draw the same everywhere plays without it
//   mod <folder>                   the settings' [game] Mod (docs/notes/mods.md), in full: <folder> relative to
//                                  the game folder (starter\Mods\LostInAstral) or in full; "harness_saves\<folder>"
//                                  is in the run's overlay, where a "# prepare:" tool writes a mod of its own
//                                  (tools/make_test_mod.py, make_mod_s19d.py). After a "waitmenu" or
//                                  "waitwindow" line the line is a step instead: a switch of mods in the main menu
//                                  (app::RequestModSwitch; "none": the game's own files), the next step once it is
//                                  made
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

// Consumes a "resolution", "widescreen", "fullscreen", "music" or "mod" scenario line; false for any other verb.
bool ParseStep(const std::string& verb, const std::string& argument);

// Applies the environment fallbacks (once, at harness start, before harness_settings' Install; `savesDir`:
// the run's harness_saves).
void Install(const char* savesDir);

// The step "modfile <archive> <file> <bytes>": the game's archive <archive> in the text resources' folder opened
// as the game opens it and <file> read through its FindText (GetFile's search, which looks in the mod's archives first
// as Rebirth's ResOverride did, and a texts.toml's text): its size must be <bytes> (0: not found; "!<n>": anything but <n>; "+": found). An error message,
// empty when it matches.
std::string ModFileStep(const std::string& argument);
// "modlist <archive> <pattern> <count>": the same archive's ListFiles(pattern) gives that many names (the mod's
// archives' and the archive's own, none of the archive's when the mod ignores it).
std::string ModListStep(const std::string& argument);
// "modarchive figures|links|textures <file> <bytes>": the size of <file> in the figure manager's, the link figure
// loader's or the texture manager's archives, in their order (the mod's extra archives among them); needs a game.
std::string ModArchiveStep(const std::string& argument);
// "modtext <name> <text>": the client world's text "string <name>" (as the interface reads it) is <text> (the rest
// of the line, UTF-8; "-": none). Records as TOML (docs/notes/mods.md): a mod's texts.toml merged over the game's.
std::string ModTextStep(const std::string& argument);
// "modack <unit> <speech> <sound>": the acks database's record <unit> has the named speech <speech> with the sound
// <sound> ("-": no such speech), as SayNamedAck finds it. Records as TOML: a mod's acks.db.toml record over the game's.
std::string ModAckStep(const std::string& argument);
// "modweapon <weapon> <min> <max>": the local player (its given items, then its party's) holds a weapon of the
// prototype named <weapon> (the rest of the line before the two numbers), whose damage as the game reckons it from the
// item's prototype and material (WeaponMinDamage, WeaponMaxDamage) is <min> to <max> ("%g"; "-": no such weapon).
// Databases as TOML (docs/notes/mods.md): a mod's items.idb.toml weapon over the game's, or one it adds.
std::string ModWeaponStep(const std::string& argument);
// "playstream <name>": the music stream is asked for <name> (an .mp3 of the stream folder, with its extension), as the main
// menu's music is; the stream's steps run on the virtual clock, so a scenario waits for it with "waitread"
// (harness_modtrace.h) and the wait takes the same world updates every run.
std::string PlayStreamStep(const std::string& argument);
// "mapfile exists <name> yes|no": the maps folder's MapFileExists, as the client's shared-file handler calls it (the
// folder follows Rebirth's Maps: the mod's or the game's).
std::string MapFileStep(const std::string& argument);

// The mod folder the scenario's "mod" step names, in full (empty without one).
std::string ModPath();
// A "mod" line's folder in full, as ModPath's (empty for "none").
std::string ModStepPath(const std::string& argument);

// Whether the run's data folder (EI_GAME_ROOT, else the exe's own) is a Rebirth install (starter\addon.dll or
// EI_Plugin.asi, where the image's start-up reads EI_Plugin.ini). Known before the game starts: the image's window
// size (960x720 on the vanilla data, 1280x720 on Rebirth's), the scenario lines "vanilla ..." / "rebirth ...", the
// settings snapshot's tree, the pinned files and the digest's install follow it.
bool RebirthData();

// The run's game folder in full (EI_GAME_ROOT, else the exe's own folder) in the game's form (backslashes), "" when
// unknown: the install the run reads and never writes.
std::string GameFolder();

} // namespace ei::verify::starter
