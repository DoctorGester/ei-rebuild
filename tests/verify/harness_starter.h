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
//   addonpath <folder>             the starter's Settings\AddonPath (the selected Rebirth mod: addon.dll's,
//                                  and the standalone image's mod_folders.h) as harness_saves\<folder>;
//                                  a "# prepare:" tool writes the mod there (tools/make_test_mod.py)
// Without them the environment variables EI_RESOLUTION and EI_WIDESCREEN (same values) apply, and
// without those the registry's own values. Rebirth's CtfMon switch is answered 0 in every run (S3e:
// addon.dll closes ctfmon at start and restarts it from its DLL_PROCESS_DETACH, which hung the exit).
//
// The game reads the resolution through its import table (RegQueryValueExA), the rebuild's WideScreen
// code reads both settings through the same slot (ui_widescreen.cpp), and addon.dll reads them
// through its own import of RegQueryValueExW when it applies its patches on the WinMain call, i.e.
// after ei_hook.dll has loaded: both slots are answered.
//
// A resolution other than the desktop's makes dgVoodoo switch the display mode in its full
// screen (seen 2026-10-04 with 1280x720), so scenarios use "desktop".

#include <string>

namespace ei::verify::starter {

// Consumes a "resolution", "widescreen", "fullscreen", "fpscursor" or "addonpath" scenario line; false for any
// other verb.
bool ParseStep(const std::string& verb, const std::string& argument);

// Applies the environment fallbacks and answers the registry reads (once, at harness start; `savesDir`: the
// run's harness_saves).
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
