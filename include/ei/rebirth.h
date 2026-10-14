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
// scenario's "rebirthflags" line. In a Rebirth launch the hook DLL makes every flag follow what
// addon.dll and EI_Plugin.asi actually apply (FollowAddon=1, the default), so flagged rewrites can
// be compared with the patched originals.
//
// ei_rebirth.ini:
//     [Rebirth]
//     Default=0             ; every flag not named below
//     FixLeversActivate=1   ; any name from rebirth_patches.inc
//     FollowAddon=1         ; Rebirth launches: flags = what the mods applied (default 1)
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

// [Rebirth] FollowAddon (default 1): in a Rebirth launch the hook DLL sets every flag to whether
// addon.dll / EI_Plugin.asi apply that patch.
bool FollowAddon();

// A rewrite's declaration that its function at `address` reproduces Rebirth patches `patches`
// (their sites inside that function). The hook installer then hooks the function in a Rebirth
// launch too, where addon.dll has patched the original, and its verify wrapper compares with the
// patched original. Declare it with EI_REBIRTH_IMPLEMENTS next to the EI_REPLACE.
struct Implements {
    std::uintptr_t address;
    const Patch* patches;
    int count;
    const Implements* next;
    Implements(std::uintptr_t address, const Patch* patches, int count);
};
// Every declaration (a list; several may name the same address).
const Implements* FirstImplements();
bool ImplementsAt(std::uintptr_t address, Patch p);

// A rewrite's declaration that the stolen bytes of its verify wrapper are also whole instructions of
// the code addon.dll wrote over the function, with no relative operand except a JMP / CALL rel32 at
// the function's start (checked by hand against addon's code). The installer then lets them end
// inside addon's site, which it otherwise refuses (a site of addon's replaced code, e.g. WideScreen's
// 194 bytes at 005F7D9E over three functions). Declare it with EI_REBIRTH_LIVE_STOLEN.
struct LiveStolen {
    std::uintptr_t address;
    const LiveStolen* next;
    explicit LiveStolen(std::uintptr_t address);
};
bool LiveStolenAt(std::uintptr_t address);

} // namespace ei::rebirth

// EI_REBIRTH_LIVE_STOLEN(0x005F7DA0);
#define EI_REBIRTH_LIVE_STOLEN(addr) \
    static const ::ei::rebirth::LiveStolen EI_REBIRTH_CAT(ei_rebirth_live_stolen_, __LINE__)(addr)

#define EI_REBIRTH_CAT_(a, b) a##b
#define EI_REBIRTH_CAT(a, b) EI_REBIRTH_CAT_(a, b)

// EI_REBIRTH_IMPLEMENTS(0x00522A80, ei::rebirth::Patch::FixLeversActivate);
#define EI_REBIRTH_IMPLEMENTS(addr, ...)                                                            \
    static const ::ei::rebirth::Patch EI_REBIRTH_CAT(ei_rebirth_list_, __LINE__)[] = {__VA_ARGS__}; \
    static const ::ei::rebirth::Implements EI_REBIRTH_CAT(ei_rebirth_impl_, __LINE__)(              \
        addr, EI_REBIRTH_CAT(ei_rebirth_list_, __LINE__),                                           \
        static_cast<int>(sizeof(EI_REBIRTH_CAT(ei_rebirth_list_, __LINE__)) / sizeof(::ei::rebirth::Patch)))
