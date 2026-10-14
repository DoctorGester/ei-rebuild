#pragma once

// Starter settings answered for a harness run (verify build only; harness_starter.cpp): the screen
// resolution and Rebirth's WideScreen switch, without writing the user's registry.
//
// Scenario lines (read when the scenario loads, before WinMain):
//   resolution <W>x<H> | desktop   the game's VideoResolution and addon.dll's VIDEORESOLUTION
//                                  ("desktop": the primary display's current mode)
//   widescreen 0|1                 the starter's Settings\WideScreen (addon.dll's switch, and the
//                                  rebuild's for its WideScreen flag)
// Without them the environment variables EI_RESOLUTION and EI_WIDESCREEN (same values) apply, and
// without those the registry's own values.
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

// Consumes a "resolution" or "widescreen" scenario line; false for any other verb.
bool ParseStep(const std::string& verb, const std::string& argument);

// Applies the environment fallbacks and answers the registry reads (once, at harness start).
void Install();

} // namespace ei::verify::starter
