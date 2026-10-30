// What the standalone image's start-up and its failure reports share (src/hook; the name stays from the hook
// framework that ran our code inside game.exe until S21c, and the vector recorders in it until H40).
//
// The standalone image (ei-rebuild.exe, docs/notes/standalone.md section 2) runs without game.exe: the game is
// linked into it and keeps game.exe's data in its own storage; our code calls every function by name, nothing stands
// at game.exe's code addresses.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ei {

// game.exe's code section (.text, 00401000 up to 0073A670): the image guards it (standalone_main.cpp reports a stray
// access there).
constexpr std::uintptr_t kGameTextBegin = 0x00401000;
constexpr std::uintptr_t kGameTextEnd = 0x0073A670;
constexpr bool InGameText(std::uintptr_t address) { return address - kGameTextBegin < kGameTextEnd - kGameTextBegin; }

// Our code's module as the logs name its addresses (<name>+offset; tools/resolve_crash.py): "ei-rebuild" (the game
// and the harness linked in). standalone.cpp.
const char* OwnModuleName();

// Called with a line naming it when the standalone image reaches game code that has no rewrite (the guarded
// code section), before the process ends: the harness (verify build) puts it in
// its report.
void SetMissingCodeHandler(void (*handler)(const char* what));
// The image reached game.exe's code at `address` with no code of ours for it (the guarded code section):
// "no code at <address>, [esp] <caller>"
// reported (the handler above, tools/resolve_crash.py), then the process ends. hook/standalone_main.cpp.
[[noreturn]] void ReportNoCode(std::uintptr_t address, std::uintptr_t caller);

// Code that runs once in the standalone image before the static initialisers and WinMain: the Rebirth flags
// are known. For data a Rebirth patch rewrites before WinMain (the widescreen layout widths).
struct StartupAction {
    void (*run)();
    const char* name;
    StartupAction* next;
};
struct StartupActionRegistrar {
    StartupAction entry;
    StartupActionRegistrar(void (*run)(), const char* name);
};
// The standalone image's start-up (StandaloneMain): loads the Rebirth flags (the game folder's profile,
// the settings file's [patches]), then runs every startup action. src/standalone/startup.cpp.
void RunStartupActions();

} // namespace ei

// Run `fn` (void()) once in the standalone image before the static initialisers (StartupAction).
#define EI_STARTUP_ACTION(fn) static ::ei::StartupActionRegistrar EI_CAT(ei_action_, __LINE__)(&fn, #fn)

#define EI_CAT_(a, b) a##b
#define EI_CAT(a, b) EI_CAT_(a, b)
