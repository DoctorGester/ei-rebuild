// The hook framework: game.exe's side (the vector recorders) and what the standalone image's start-up shares with it.
//
// The standalone image (ei_standalone.exe, docs/notes/standalone.md section 2) runs without game.exe: it
// loads ei_hook.dll, which keeps game.exe's data in its own storage; our code calls every function by name, nothing
// stands at game.exe's code addresses. In game.exe, which runs only as the original game (the original's harness),
// nothing of ours is installed but the vector recorders (EI_RECORD, with EI_VECTORS=1).
#pragma once

#include <cstddef>
#include <cstdint>

namespace ei {

// Image base of the original game.exe (no relocations, always loaded here).
constexpr std::uintptr_t kImageBase = 0x00400000;

// A vector recorder (EI_RECORD): runs the original through a trampoline. `stolen` must cover whole instructions
// with no relative operands (tools/check_stolen.py).
struct HookEntry {
    std::uintptr_t address;   // VA of the original function in game.exe
    void* recorder;
    const char* name;
    HookEntry* next;
    int stolen;               // the trampoline's prologue bytes
    void** original;          // receives the trampoline (original function) address
};

// Registers a recorder at static-initialisation time. Installed by InstallHooks().
struct HookRegistrar {
    HookEntry entry;
    HookRegistrar(std::uintptr_t address, void* recorder, const char* name, int stolen, void** original);
};

// game.exe with EI_VECTORS=1: installs every recorder; returns the number installed. hook.cpp.
int InstallHooks();

// game.exe's code section (.text, 00401000 up to 0073A670): the addresses the function-to-original map names (the
// image guards it: standalone_main.cpp reports a stray access there).
constexpr std::uintptr_t kGameTextBegin = 0x00401000;
constexpr std::uintptr_t kGameTextEnd = 0x0073A670;
constexpr bool InGameText(std::uintptr_t address) { return address - kGameTextBegin < kGameTextEnd - kGameTextBegin; }

// The harness (verify build) patches `size` bytes of game code at `address` (`what`, a string
// literal). Called before InstallHooks, it marks the functions holding the bytes as the harness's:
// their recorders are left out as expected, not as unexpected skips.
void NoteHarnessPatch(std::uintptr_t address, std::size_t size, const char* what);
// The `what` of a noted harness patch overlapping [address, address + size), or nullptr.
const char* HarnessPatchAt(std::uintptr_t address, std::size_t size);
// Recorders InstallHooks left out for a reason other than the harness's patches (a second recorder on one
// address, stolen bytes that differ from game.exe's file, ...): they never run. The harness fails a run with any
// (SV6).
int UnexpectedSkips();

// True inside ei_standalone.exe (src/standalone/host.cpp, docs/notes/standalone.md section 2): nothing of
// game.exe at its addresses, its data in ei_hook.dll's storage, its resources the host's own. standalone.cpp.
bool Standalone();
// Called with a line naming it when the standalone image reaches game code that has no rewrite (the guarded
// code section, a compiled table's unported slot), before the process ends: the harness (verify build) puts it in
// its report.
void SetMissingCodeHandler(void (*handler)(const char* what));
// The image reached game.exe's code at `address` with no code of ours for it (the guarded code section, a
// compiled table's unported slot: compiled_vtable.h's UnportedSlot): "no code at <address>, [esp] <caller>"
// reported (the handler above, tools/resolve_crash.py), then the process ends. hook/standalone_main.cpp.
[[noreturn]] void ReportNoCode(std::uintptr_t address, std::uintptr_t caller);

// Code that runs once in the standalone image before the static initialisers and WinMain: the Rebirth flags
// are known. For data a Rebirth patch rewrites before WinMain (the widescreen layout widths). Never run in
// game.exe (the original game).
struct StartupAction {
    void (*run)();
    const char* name;
    StartupAction* next;
};
struct StartupActionRegistrar {
    StartupAction entry;
    StartupActionRegistrar(void (*run)(), const char* name);
};
// The standalone image's start-up (EiStandaloneMain): loads the Rebirth flags (ei_rebirth.ini), then runs every
// startup action. hook.cpp.
void RunStartupActions();

// game.exe (the original game's harness): arranges for InstallHooks() (the recorders), the coverage
// breakpoints and the profiler to go in when WinMain is entered (after other mods patch).
void InstallHooksAtWinMain();
// game.exe: `action` runs when WinMain is entered (after addon.dll applied its patches), before the
// recorders go in: the harness's addon fix set (tests/verify/harness_addon.h).
void SetOriginalWinMainAction(void (*action)());

} // namespace ei

// Verify build only (tests/vectors/record_*.cpp): in game.exe with EI_VECTORS=1 (the original game, the
// recorders its only hooks) route `addr` to `recorder`, and store in `orig` a trampoline running the
// original (its first `stolen` bytes, then a jump back), which the recorder calls and samples. `rewrite` is our
// function the replays (tests/vectors/replay_*.cpp) call with what this records: the compiler checks the name, and
// tools/check_signatures.py its stack arguments against the original's RET n.
#define EI_RECORD(addr, recorder, stolen, orig, rewrite) \
    using EI_CAT(ei_record_rewrite_, __LINE__) = decltype(&rewrite); \
    static ::ei::HookRegistrar EI_CAT(ei_record_, __LINE__)( \
        addr, reinterpret_cast<void*>(&recorder), #recorder, stolen, reinterpret_cast<void**>(&orig))

// The original of a rewritten function (C20d): nothing reaches game.exe's address, our code calls the function by
// name. EI_REWRITTEN(0x..., Fn); leaves a record {address, &Fn, file, line} in the section .eiorg of the module,
// which nothing reads at run time: the compiler checks the name, the record keeps the function's own copy in the
// image, and tools/check_registrations.py reads it (one original per address). These lines are the tools'
// function-to-original map (check_signatures.registrations(): check_signatures; coverage.py, save_trace.py,
// ui_census_report.py, named_data.py, gen_short_functions.py, progress.py's rewritten count).
#pragma section(".eiorg", read, write)
namespace ei {
// `code` points at a constant holding &fn (a member pointer of a class with several bases is two words, C28l), or
// at the name of a constructor or destructor (EI_REWRITTEN_SPECIAL); the record stays four words.
struct OriginRecord {
    std::uintptr_t address;
    const void* code;
    const char* file;
    int line;
};
} // namespace ei
#define EI_REWRITTEN(addr, fn) \
    static constexpr auto EI_CAT(ei_rewritten_fn_, __LINE__) = &fn; \
    __declspec(allocate(".eiorg")) static ::ei::OriginRecord EI_CAT(ei_rewritten_, __LINE__) = \
        {addr, &EI_CAT(ei_rewritten_fn_, __LINE__), __FILE__, __LINE__}
// The same for a constructor or destructor, which has no address to take (C28): `EI_REWRITTEN_SPECIAL(0x...,
// CItem::CItem);`, `(0x..., CItem::~CItem);` (also for the original's scalar deleting destructor). The record holds
// the name; the tools read the line like an EI_REWRITTEN one.
#define EI_REWRITTEN_SPECIAL(addr, name) \
    __declspec(allocate(".eiorg")) static ::ei::OriginRecord EI_CAT(ei_rewritten_, __LINE__) = \
        {addr, #name, __FILE__, __LINE__}

// Run `fn` (void()) once in the standalone image before the static initialisers (StartupAction).
#define EI_STARTUP_ACTION(fn) static ::ei::StartupActionRegistrar EI_CAT(ei_action_, __LINE__)(&fn, #fn)

#define EI_CAT_(a, b) a##b
#define EI_CAT(a, b) EI_CAT_(a, b)
