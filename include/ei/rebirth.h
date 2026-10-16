// Rebirth's patches as optional behaviour of the rebuilt code.
//
// The Rebirth community edition changes game.exe in memory: starter\addon.dll (v0.12.4.2) applies
// its patches at WinMain and EI_Plugin.asi applies its features when the sound system starts
// (docs/notes/rebirth.md lists all of them). Rewritten code reproduces a patch behind a flag named
// exactly as addon.log names the patch, so a Rebirth patch maps 1:1 to a flag:
//
//     if (ei::rebirth::Enabled(ei::rebirth::Patch::FixLeversActivate)) { ...patched behaviour... }
//
// Every flag is off by default (the original 1.06). They are read once at startup, before any
// rewritten code runs: ei_rebirth.ini next to ei_hook.dll (next to the exe in the standalone
// build), then the EI_REBIRTH environment variable (ei_launch --rebirth), then a harness
// scenario's "rebirthflags" line. The standalone image never loads addon.dll or EI_Plugin.asi:
// these flags alone decide (config/ei_rebirth_standalone.ini for the Rebirth install's runs).
//
// ei_rebirth.ini:
//     [Rebirth]
//     Default=0             ; every flag not named below
//     FixLeversActivate=1   ; any name from rebirth_patches.inc
//     SecondMaster=         ; EI_Plugin NewMaster's second master server, host[:port] (default empty: none)
#pragma once

#include <cstdint>

namespace ei::rebirth {

enum class Patch : std::uint8_t {
#define EI_REBIRTH_PATCH(name) name,
#include "rebirth_patches.inc"
#undef EI_REBIRTH_PATCH
    Count
};
constexpr int kPatchCount = static_cast<int>(Patch::Count);

namespace detail {
extern bool g_enabled[kPatchCount];
}

// Does the rewritten code apply Rebirth's patch `p`?
inline bool Enabled(Patch p) { return detail::g_enabled[static_cast<int>(p)]; }

void Set(Patch p, bool on);

// The player count EI_Plugin's NPlayers feature (Patch::EiPluginNPlayers) is configured with
// (EI_Plugin.ini [CONFIG] NPlayers; the plugin's default 7 until the hook DLL has read the ini).
int PluginNPlayers();
void SetPluginNPlayers(int players);

// EI_Plugin.ini [CONFIG] Pause, the mode EI_Plugin's Pause feature (Patch::EiPluginPause) runs in: 1 every
// pause and resume announced (the leader only), 2 also by any player, 3 no pause at all (default 1 until
// the hook DLL has read the ini; CPlayerServer::OnPause, CWorld::SetPaused).
int PluginPause();
void SetPluginPause(int mode);

// The second master server of EI_Plugin's NewMaster (Patch::EiPluginNewMaster; the plugin's own is
// hat.gipath.org:28004): "host[:port]" (port 28004 when left out), from ei_rebirth.ini [Rebirth]
// SecondMaster or a spec's "SecondMaster=<address>". Empty (the default, and every harness run that
// does not name one): no second master at all, the game's own code (net_second_master.h).
const char* PluginSecondMaster();
void SetPluginSecondMaster(const char* address);

const char* Name(Patch p);
bool FromName(const char* name, Patch* out);

// "all", "none", or patch names separated by commas or spaces, each optionally "=0" / "=1".
// Unknown names are skipped; returns false if there was one.
bool ApplySpec(const char* spec);

// Queues a spec to apply after the ini and the environment (the harness, before WinMain).
void QueueSpec(const char* spec);

// Reads the settings: [Rebirth] of `iniPath` (a missing file changes nothing), the EI_REBIRTH
// environment variable, then the queued specs. Called once, before hooks are installed.
void LoadSettings(const char* iniPath);

} // namespace ei::rebirth
