#pragma once

// The original game with Rebirth's bug fixes only (verify build only; harness_addon.cpp, SV9).
//
// The vanilla goldens are recorded from game.exe on the vanilla data with the patches the standalone has on
// by default (include/ei/rebirth.h: Kind::Fix), as addon.dll itself applies them: run_all's vanilla install
// starts run\vanilla-fixes\game.exe (run\vanilla plus Rebirth's addon loader dinput.dll; tools/
// setup_fixes_run.cmd) with EI_ADDON_FLAGS=1. addon.dll has no switch per patch (its main table applies every
// entry), so when WinMain is entered (after addon applied its tables) the harness keeps the sites of the
// patches whose Rebirth flag is on in this run (the defaults, ei_rebirth.ini, EI_REBIRTH, a scenario's
// rebirthflags) and writes game.exe's own bytes back over every other site addon changed (an import slot gets
// the import's address). Then it checks, against game.exe on disk, that every site of every kept patch is
// live and that .text and .rdata hold no other change but the harness's own patches; anything else fails the
// run ("addon: ..."). addon's writes into the Rebirth install (starter\addon.log, Dumps) go to the run's
// overlay, under harness_saves\rebirth_install\ (harness.cpp ShadowOf).
//
// Such a run counts as the vanilla install (starter::RebirthInstall is false): "vanilla <step>" lines run,
// the digest and the UI census say vanilla.

#include <string>

namespace ei::verify::addon {

// EI_ADDON_FLAGS=1 in the environment.
bool Active();

// At harness start: registers the WinMain check (game.exe only).
void Install();

// The run's addon problems, "; "-separated; empty when there were none.
std::string Failure();

} // namespace ei::verify::addon
