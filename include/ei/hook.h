// Function replacement framework.
//
// The standalone image (ei_standalone.exe, docs/notes/standalone.md section 2) runs without game.exe: it
// loads ei_hook.dll, which keeps game.exe's data in its own storage and whose InstallHooks takes the
// registrations as the code for their addresses (CodeAddress; nothing of game.exe is at its addresses). In game.exe, which runs only as the original game (the original's harness), nothing of ours
// is installed but the vector recorders (EI_RECORD, with EI_VECTORS=1).
#pragma once

#include <cstdint>
#include <cstring>

namespace ei {

// Image base of the original game.exe (no relocations, always loaded here).
constexpr std::uintptr_t kImageBase = 0x00400000;

struct HookEntry {
    std::uintptr_t address;   // VA of the original function in game.exe
    void* replacement;        // our implementation
    const char* name;
    HookEntry* next;
    int stolen = 0;           // recorders: build a trampoline from this many prologue bytes
    void** original = nullptr; // receives the trampoline (original function) address
    bool record = false;      // vector recorder (EI_RECORD): installed only in game.exe with EI_VECTORS=1
};

// Registers a replacement at static-initialisation time. Installed by InstallHooks().
struct HookRegistrar {
    HookEntry entry;
    HookRegistrar(std::uintptr_t address, void* replacement, const char* name);
    // Vector recorder (EI_RECORD): runs the original through a trampoline. `stolen` must cover
    // whole instructions with no relative operands (tools/check_stolen.py).
    HookRegistrar(std::uintptr_t address, void* recorder, const char* name, int stolen, void** original);
};

// Address of a member function, for registering class methods as replacements. Valid for non-virtual members.
// A class with several bases (the windows, C28l) has two-word member pointers: the code and the adjustment of
// `this`, which must be 0 (a member of the first base chain); null otherwise.
template <typename T>
void* MemberAddress(T member) {
    static_assert(sizeof(member) == sizeof(void*) || sizeof(member) == 2 * sizeof(void*),
                  "virtual inheritance member pointer");
    struct {
        void* code;
        int adjust;
    } p{};
    std::memcpy(&p, &member, sizeof(member));
    return p.adjust == 0 ? p.code : nullptr;
}

// Installs every registered hook; returns the number installed. Implemented in hook.cpp.
int InstallHooks();

// game.exe's code section (.text, 00401000 up to 0073A670): the addresses registrations name (the image guards it, image_code.cpp; standalone.cpp reports a stray access there).
constexpr std::uintptr_t kGameTextBegin = 0x00401000;
constexpr std::uintptr_t kGameTextEnd = 0x0073A670;
constexpr bool InGameText(std::uintptr_t address) { return address - kGameTextBegin < kGameTextEnd - kGameTextBegin; }
// game.exe's WinMain (app_main.cpp's GameMain): the harness installs the hooks when it starts (hook.cpp), the
// image calls it (standalone.cpp).
constexpr std::uintptr_t kGameWinMain = 0x00456440;

// What runs for game.exe's function at `address` in the standalone image (S18i, src/standalone/image_code.cpp),
// whose code section is guarded: the replacement registered for it (from InstallHooks on), else a stub that reports
// there is no code at the address. An address outside game.exe's code section is its own code. Not for game.exe's
// runs, where nothing of ours runs: the harness calls the original there (tests/verify/harness_original.h; H30).
std::uintptr_t CodeAddress(std::uintptr_t address);
template <typename F>
F Code(std::uintptr_t address) {
    return reinterpret_cast<F>(CodeAddress(address));
}
// The harness: how often CodeAddress was asked.
std::uint64_t CodeLookups();
// Whether CodeAddress has code for `address` other than the stub.
bool HasCode(std::uintptr_t address);

// The harness (verify build) patches `size` bytes of game code at `address` (`what`, a string
// literal). Called before InstallHooks, it marks the functions holding the bytes as the harness's:
// in game.exe their recorders are left out as expected, not as unexpected skips.
void NoteHarnessPatch(std::uintptr_t address, std::size_t size, const char* what);
// The `what` of a noted harness patch overlapping [address, address + size), or nullptr.
const char* HarnessPatchAt(std::uintptr_t address, std::size_t size);
// Registrations InstallHooks left out for a reason other than the harness's patches (an unknown
// function start, a second hook on one address, ...): their rewrites never run. The harness fails a
// run with any (SV6).
int UnexpectedSkips();

// True inside ei_standalone.exe (src/standalone/host.cpp, docs/notes/standalone.md section 2): nothing of
// game.exe at its addresses, its data in ei_hook.dll's storage, its resources the host's own; the plain
// replacements are game.exe's code (CodeAddress). standalone.cpp.
bool Standalone();
// Called with a line naming it when the standalone image reaches game code that has no rewrite (a stub of
// CodeAddress, or the guarded code section), before the process ends: the harness (verify build) puts it in its report.
void SetMissingCodeHandler(void (*handler)(const char* what));
// The image reached game.exe's code at `address` with no code of ours for it (a stub, the guarded code section, a
// compiled table's unported slot: compiled_vtable.h's UnportedSlot): "no code at <address> (...), [esp] <caller>"
// reported (the handler above, tools/resolve_crash.py), then the process ends. hook/standalone.cpp.
[[noreturn]] void ReportNoCode(std::uintptr_t address, std::uintptr_t caller);

// Code that runs once in the standalone image, right before the registrations are taken: the Rebirth flags
// are known, nothing is hooked yet. For data a Rebirth patch rewrites before WinMain (the widescreen
// layout widths). Never run in game.exe (the original game).
struct StartupAction {
    void (*run)();
    const char* name;
    StartupAction* next;
};
struct StartupActionRegistrar {
    StartupAction entry;
    StartupActionRegistrar(void (*run)(), const char* name);
};

// game.exe (the original game's harness): arranges for InstallHooks() (the recorders), the coverage
// breakpoints and the profiler to go in when WinMain is entered (after other mods patch).
void InstallHooksAtWinMain();
// game.exe: `action` runs when WinMain is entered (after addon.dll applied its patches), before the
// recorders go in: the harness's addon fix set (tests/verify/harness_addon.h).
void SetOriginalWinMainAction(void (*action)());

} // namespace ei

// Every registration macro below also leaves a record of the address, kind and place in the section
// .eireg of the module (data nothing reads at run time; not const, or the compiler drops it): tools/check_registrations.py reads the
// built ei_hook.dll and fails the build when one address has two registrations of a kind that
// InstallHooks would install (two plain replacements, two recorders).
#pragma section(".eireg", read, write)
namespace ei {
enum RegKind : int { kRegReplace, kRegStandalone, kRegShort, kRegRecord };
struct RegRecord {
    std::uintptr_t address;
    const char* file;
    int line;
    int kind;
};
} // namespace ei
#define EI_REG_RECORD(prefix, addr, kind) \
    __declspec(allocate(".eireg")) static ::ei::RegRecord EI_CAT(prefix, __LINE__) = {addr, __FILE__, __LINE__, kind}

// Replace the function at `addr` with a free function or static function.
#define EI_REPLACE(addr, fn) \
    EI_REG_RECORD(ei_reg_replace_, addr, ::ei::kRegReplace); \
    static ::ei::HookRegistrar EI_CAT(ei_hook_, __LINE__)(addr, reinterpret_cast<void*>(&fn), #fn)

// Replace the method at `addr` with a C++ member function.
#define EI_REPLACE_METHOD(addr, method) \
    EI_REG_RECORD(ei_reg_method_, addr, ::ei::kRegReplace); \
    static ::ei::HookRegistrar EI_CAT(ei_hook_, __LINE__)(addr, ::ei::MemberAddress(&method), #method)

// Replace the library function at `addr`: the game's runtime (the CRT heap, operator new's handler, the
// atexit list), which the image has instead of the exe's library code. Installed as EI_REPLACE; a kind
// of its own for check_registrations and the dashboard's counts.
#define EI_REPLACE_STANDALONE(addr, fn) \
    EI_REG_RECORD(ei_reg_standalone_, addr, ::ei::kRegStandalone); \
    static ::ei::HookRegistrar EI_CAT(ei_standalone_, __LINE__)(addr, reinterpret_cast<void*>(&fn), #fn)

// Replace the function under five bytes at `addr` (src/game/short_functions.cpp).
#define EI_REPLACE_SHORT(addr, fn) \
    EI_REG_RECORD(ei_reg_short_, addr, ::ei::kRegShort); \
    static ::ei::HookRegistrar EI_CAT(ei_short_, __LINE__)(addr, reinterpret_cast<void*>(&fn), #fn)

// Verify build only (tests/vectors/record_*.cpp): in game.exe with EI_VECTORS=1 (the original game, the
// recorders its only hooks) route `addr` to `recorder`, and store in `orig` a trampoline running the
// original (its first `stolen` bytes, then a jump back), which the recorder calls and samples.
#define EI_RECORD(addr, recorder, stolen, orig) \
    EI_REG_RECORD(ei_reg_record_, addr, ::ei::kRegRecord); \
    static ::ei::HookRegistrar EI_CAT(ei_record_, __LINE__)( \
        addr, reinterpret_cast<void*>(&recorder), #recorder, stolen, reinterpret_cast<void**>(&orig))

// The original of a rewritten function that has no registration (C20d): nothing reaches game.exe's address, our code
// calls the function by name. EI_REWRITTEN(0x..., Fn); leaves a record {address, &Fn, file, line} in the section
// .eiorg of the module, which nothing reads at run time and InstallHooks does not take: the compiler checks the name,
// the record keeps the function's own copy in the image (as a registration did), and check_registrations reads it
// (one original per address, none also registered). The tools' function-to-original map is the registrations and
// these lines together (check_signatures.registrations(): check_signatures; coverage.py,
// save_trace.py, ui_census_report.py, progress.py's rewritten count). tools/registration_readers.py
// --drop writes one in place of each registration it drops.
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

// Run `fn` (void()) once in the standalone image before the registrations are taken (StartupAction).
#define EI_STARTUP_ACTION(fn) static ::ei::StartupActionRegistrar EI_CAT(ei_action_, __LINE__)(&fn, #fn)

#define EI_CAT_(a, b) a##b
#define EI_CAT(a, b) EI_CAT_(a, b)
