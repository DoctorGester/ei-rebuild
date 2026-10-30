// Rebirth's patches as optional behaviour of the rebuilt code.
//
// The Rebirth community edition changes game.exe in memory: starter\addon.dll (v0.12.4.2) applies
// its patches at WinMain and EI_Plugin.asi applies its features when the sound system starts
// (docs/notes/rebirth.md lists all of them). Rewritten code reproduces a patch behind a flag named
// exactly as addon.log names the patch, so a Rebirth patch maps 1:1 to a flag:
//
//     if (ei::rebirth::Enabled(ei::rebirth::Patch::CameraPatch)) { ...patched behaviour... }
//
// Rebirth's bug fixes and its harmless additions are the rebuilt code's own behaviour (C68) and have no flag. Every
// flag is off by default and can be switched either way. Every flag is off until Reset, which the standalone image's
// start-up calls once, before any rewritten code runs (src/standalone/startup.cpp; ei_vectors.exe, which replays the
// original's vectors, never does): the defaults, then the game folder's profile (Rebirth's in a Rebirth install:
// config/rebirth_profile.ini; a launch that names its flags in EI_REBIRTH takes none), then the settings file's
// [patches] (src/game/settings.h), then the EI_REBIRTH environment variable (ei_launch --rebirth) and a harness
// scenario's "rebirthflags" lines (ApplyOverrides). The standalone image never loads addon.dll or EI_Plugin.asi:
// these flags alone decide. "none" is the original 1.06.
//
// ei_settings.ini (a comment is a line of its own: the rest of a value line is its value):
//     [patches]
//     ; any name from rebirth_patches.inc, true or false (absent: the profile's)
//     CameraPatch = true
//     ; EI_Plugin's player count and pause mode (absent: the profile's)
//     NPlayers = 7
//     Pause = 1
//     ; EI_Plugin NewMaster's second master server, host[:port] (default empty: none)
//     SecondMaster =
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

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
// (EI_Plugin.ini [CONFIG] NPlayers; the plugin's default 7 until the start-up has read the settings).
int PluginNPlayers();
void SetPluginNPlayers(int players);

// EI_Plugin.ini [CONFIG] Pause, the mode EI_Plugin's Pause feature (Patch::EiPluginPause) runs in: 1 every
// pause and resume announced (the leader only), 2 also by any player, 3 no pause at all (default 1 until
// the start-up has read the settings; PlayerServer::OnPause, World::SetPaused).
int PluginPause();
void SetPluginPause(int mode);

// The second master server of EI_Plugin's NewMaster (Patch::EiPluginNewMaster; the plugin's own is
// hat.gipath.org:28004): "host[:port]" (port 28004 when left out), from the settings file's [patches]
// SecondMaster or a spec's "SecondMaster=<address>". Empty (the default, and every harness run that
// does not name one): no second master at all, the game's own code (net_second_master.h).
const std::string& PluginSecondMaster();
void SetPluginSecondMaster(std::string_view address);

const char* Name(Patch p);
bool FromName(const char* name, Patch* out);

// "all", "none", "defaults", or patch names separated by commas or spaces, each optionally "=0" / "=1".
// Unknown names are skipped; returns false if there was one.
bool ApplySpec(const char* spec);

// Queues a spec to apply after the settings file and the environment (the harness, before WinMain).
void QueueSpec(const char* spec);

// The defaults, then the spec `profile` when one is given; the plugin's values and SecondMaster back to theirs.
void Reset(const char* profile = nullptr);

// The specs that win over the settings file: the EI_REBIRTH environment variable, then the queued specs.
void ApplyOverrides();

} // namespace ei::rebirth
