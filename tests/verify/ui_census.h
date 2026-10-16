#pragma once

// Interface drawing census (verify build only; ticket N0, docs/notes/ui-native.md and
// docs/notes/ui-census.md): which GDI primitives each window surface is drawn with, from where, how
// often, who reads it back, and changes to its pixels that no hooked call explains. Switched on by
// the scenario step "uicensus" or EI_UI_CENSUS=1; needs the window surfaces in memory DCs
// (the standalone image). Writes ui_census.csv next to the harness report.

namespace ei::verify::census {

// Turns the census on for this run (the "uicensus" step, read with the scenario).
void Enable();
// On when Enable was called or EI_UI_CENSUS is 1.
bool Enabled();

// Patches game.exe's import slots and the owner observer (from the harness's start).
void Install();

// A frame has been drawn (the harness's message loop hook): hooks modules loaded since (addon.dll),
// checksums every known surface and counts its frame.
void Frame();

// Writes ui_census.csv in the output folder (with the harness report).
void Write();

} // namespace ei::verify::census
