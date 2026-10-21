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
//   fpscursor 0|1                  the game's FPSIndependentCursor (options +0x80: on, the cursor runs in
//                                  mode 2 with its drawing thread 005E5690; off, mode 1 without one)
//   music <volume>                 the game's AudioStreaming (the streamed music's volume, 0..100); 0: no
//                                  streamed music. The stream thread plays in real time, and the music
//                                  system's random draws follow the track it reports (SV5d5: a fight's
//                                  combat track is picked only when the playing one is not it), so a run
//                                  whose fights must draw the same everywhere plays without it
//   addonpath <folder>             the starter's Settings\AddonPath (the selected Rebirth mod: addon.dll's,
//                                  and the standalone image's mod_folders.h) as harness_saves\<folder>;
//                                  a "# prepare:" tool writes the mod there (tools/make_test_mod.py)
// Without them the environment variables EI_RESOLUTION and EI_WIDESCREEN (same values) apply, and
// without those the registry snapshot's values (harness_settings.h). Rebirth's CtfMon switch is answered
// 0 in every run (S3e: addon.dll closes ctfmon at start and restarts it from its DLL_PROCESS_DETACH, which
// hung the exit).
//
// The answers are given by harness_settings.cpp, which answers every read of the game's and the starter's
// keys (the game's, the image's and addon.dll's), these by value name before its snapshot.
//
// A resolution other than the desktop's makes dgVoodoo switch the display mode in its full
// screen (seen 2026-10-04 with 1280x720), so scenarios use "desktop".

#include <string>

namespace ei::verify::starter {

// The run's answer for the registry value `name` (any key): its type, data and size (`dword` holds a
// DWORD answer); false when the run leaves it to the snapshot.
bool Answer(const char* name, unsigned long* type, const unsigned char** data, unsigned long* size, unsigned long* dword);

// Consumes a "resolution", "widescreen", "fullscreen", "fpscursor", "music" or "addonpath" scenario line; false for any
// other verb.
bool ParseStep(const std::string& verb, const std::string& argument);

// Applies the environment fallbacks (once, at harness start, before harness_settings' Install; `savesDir`:
// the run's harness_saves).
void Install(const char* savesDir);

// The step "modfile <archive> <file> <bytes>": the game's archive <archive> in the text resources' folder opened
// through OpenResource's address and <file> read through GetFile's vtable slot (Rebirth's ResOverride, addon's
// or the standalone image's, looks in the mod's archives first): its size must be <bytes> (0: not found). An
// error message, empty when it matches.
std::string ModFileStep(const std::string& argument);

// Whether this run plays the Rebirth install: addon.dll is loaded (game.exe), or the standalone image runs
// with Rebirth flags (rebirth_hook::SettingsOnly; S19). Scenario lines "vanilla ..." / "rebirth ..." and the
// digest's and the UI census's install follow it.
bool RebirthInstall();

} // namespace ei::verify::starter
