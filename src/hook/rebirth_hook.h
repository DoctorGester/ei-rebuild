// Rebirth support for the hook installer (hook.cpp): where addon.dll and EI_Plugin.asi patch
// game.exe, which of their patches this launch has, the flags following them, and whether a
// rewritten function can be hooked where they patched the original.
#pragma once

#include "ei/rebirth.h"

#include <cstdint>
#include <vector>

namespace ei::rebirth_hook {

// At WinMain, before any hook: finds which patches addon.dll applied (their sites differ from
// game.exe on disk) and which EI_Plugin.asi will apply when the sound system loads it (from
// EI_Plugin.ini and addon's mode word), logs them, writes rebirth_sites_live.txt next to the DLL,
// and with FollowAddon sets every flag to whether the patch is applied.
void Detect(const std::vector<unsigned char>& file);

// In the standalone image instead of Detect: no addon.dll and no EI_Plugin.asi (the host and Miles keep
// them out), so the flags are ei_rebirth.ini's (and EI_REBIRTH's) alone; EI_Plugin's player count for
// EiPluginNPlayers is the ini's [Rebirth] NPlayers (default 7, the plugin's). Logs the flags.
void SettingsOnly(const char* iniPath);

// True when addon.dll or EI_Plugin.asi patch this launch.
bool RebirthLaunch();

enum class Verdict {
    Unpatched,    // no Rebirth patch in the function: install as usual
    Implemented,  // patched, every patch there is implemented by the rewrite and enabled
    Skip,         // patched by something the rewrite does not reproduce (or its flag is off)
};

// Decides for the rewrite of the function at `address` (`size` bytes; `orig` its bytes on disk)
// whose wrapper steals `stolen` bytes (0 for a plain replacement). Writes the reason to `why`.
Verdict Check(std::uintptr_t address, std::uint32_t size, const unsigned char* orig, int stolen,
              char* why, std::size_t whySize);

// A verify wrapper compares with the original in memory. True when the rewrite applies an
// enabled patch that the original here does not have (a vanilla launch with the flag on): the
// function then runs without its wrapper. Writes the patch to `why`.
bool FlagWithoutOriginal(std::uintptr_t address, std::uint32_t size, const unsigned char* orig,
                         char* why, std::size_t whySize);

// After MakeTrampoline copied `stolen` live bytes of `address` to `trampoline`: re-targets the
// CALL/JMP rel32 that addon.dll wrote at the start of a site in that range, so the trampoline
// still reaches addon's code. Returns false if a site in the range cannot be moved.
bool RelocateStolen(std::uintptr_t address, int stolen, unsigned char* trampoline);

} // namespace ei::rebirth_hook
