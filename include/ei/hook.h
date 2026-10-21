// Function replacement framework.
//
// The standalone image (ei_standalone.exe, docs/notes/standalone.md section 2) lays out game.exe's
// headers and data without its code and loads ei_hook.dll, whose InstallHooks takes the registrations
// as the code for their addresses (CodeAddress; S18i: game.exe's code section is guarded, nothing runs
// there). In game.exe, which runs only as the original game (the original's harness), nothing of ours
// is installed but the vector recorders (EI_RECORD, with EI_VECTORS=1). EI_ORIGINAL forwarders name
// original addresses.
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
    bool dispatch = false;    // EI_REPLACE_DISPATCH: the replacement is a deliberate vtable dispatch
};

// Registers a replacement at static-initialisation time. Installed by InstallHooks().
struct HookRegistrar {
    HookEntry entry;
    HookRegistrar(std::uintptr_t address, void* replacement, const char* name);
    // Vector recorder (EI_RECORD): runs the original through a trampoline. `stolen` must cover
    // whole instructions with no relative operands (tools/check_stolen.py).
    HookRegistrar(std::uintptr_t address, void* recorder, const char* name, int stolen, void** original);
};

// A game function that is itself a bare vtable dispatch (MOV EAX,[ECX]; JMP [EAX+n]) to another slot:
// the replacement is compiled to the same bytes, and the installer checks that its dispatch bytes
// equal the original's.
struct DispatchRegistrar {
    HookRegistrar registrar;
    DispatchRegistrar(std::uintptr_t address, void* replacement, const char* name)
        : registrar(address, replacement, name) {
        registrar.entry.dispatch = true;
    }
};

// Address of a member function, for registering class methods as replacements.
// Valid for non-virtual members of classes with single inheritance (the only kind we hook).
template <typename T>
void* MemberAddress(T member) {
    void* p;
    static_assert(sizeof(member) == sizeof(p), "multiple/virtual inheritance member pointer");
    std::memcpy(&p, &member, sizeof(p));
    return p;
}

// Installs every registered hook; returns the number installed. Implemented in hook.cpp.
int InstallHooks();

// What runs for game.exe's function at `address` (S18i, src/standalone/image_code.cpp): in game.exe the
// address itself (the original); in the standalone image, whose code section is guarded, the replacement
// registered for it (from InstallHooks on), the harness's (RedirectCode), else a stub that reports there is
// no code at the address. An address outside game.exe's code section is its own code.
std::uintptr_t CodeAddress(std::uintptr_t address);
template <typename F>
F Code(std::uintptr_t address) {
    return reinterpret_cast<F>(CodeAddress(address));
}
// Whether CodeAddress has code for `address` other than the stub (always in game.exe).
bool HasCode(std::uintptr_t address);
// The image: the harness's replacement for game.exe's function at `address` (what a JMP written there is in
// game.exe): CodeAddress and the code cells of game.exe's data that hold the address lead to `to`.
void RedirectCode(std::uintptr_t address, const void* to);
// The image: a code pointer the harness took out of game.exe's data before the code cells were pointed at our
// code (a vtable slot it replaces at start-up) is resolved with them (at once when they are already).
void FixUpCodePointer(std::uintptr_t* pointer);

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

// True inside ei_standalone.exe (src/standalone/host.cpp, docs/notes/standalone.md section 2): no
// game.exe code, only its headers at kImageBase and its data in ei_hook.dll's storage; the plain
// replacements are game.exe's code (CodeAddress). standalone.cpp.
bool Standalone();
// The game.exe file the standalone host built the image from.
const char* StandaloneExePath();
// Called with a line naming it when the standalone image reaches game code that has no rewrite (a stub of
// CodeAddress, or the guarded code section), before the process ends: the harness (verify build) puts it in its report.
void SetMissingCodeHandler(void (*handler)(const char* what));

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
enum RegKind : int { kRegReplace, kRegStandalone, kRegShort, kRegDispatch, kRegRecord };
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

// Register a method whose body is a tail call through the vtable, the same dispatch as the original's
// own (DestroyEffect, SpecRemove, SpecRefresh); see DispatchRegistrar.
#define EI_REPLACE_DISPATCH(addr, method) \
    EI_REG_RECORD(ei_reg_dispatch_, addr, ::ei::kRegDispatch); \
    static ::ei::DispatchRegistrar EI_CAT(ei_dispatch_, __LINE__)(addr, ::ei::MemberAddress(&method), #method)

// Replace the library function at `addr`: the game's runtime (the CRT heap, operator new's handler, the
// atexit list), which the image has instead of the exe's library code. Installed as EI_REPLACE; a kind
// of its own for check_registrations and the dashboard's counts.
#define EI_REPLACE_STANDALONE(addr, fn) \
    EI_REG_RECORD(ei_reg_standalone_, addr, ::ei::kRegStandalone); \
    static ::ei::HookRegistrar EI_CAT(ei_standalone_, __LINE__)(addr, reinterpret_cast<void*>(&fn), #fn)

// Replace the function under five bytes at `addr` (src/game/short_functions.cpp; tools/check_short_functions.py
// checks the kinds).
#define EI_REPLACE_SHORT(addr, fn) \
    EI_REG_RECORD(ei_reg_short_, addr, ::ei::kRegShort); \
    static ::ei::HookRegistrar EI_CAT(ei_short_, __LINE__)(addr, reinterpret_cast<void*>(&fn), #fn)

// Define a function body that forwards to original code not rewritten yet. Works for any calling
// convention (including __thiscall methods) because it is a bare jump with the stack untouched: to
// CodeAddress(addr) (ei_code_dispatch, src/standalone/image_code.cpp), the address itself in game.exe.
// ei_vectors.exe has no game.exe: there the jump goes to the replacement registered for `addr`, as in
// the standalone image, or to a replay stand-in (tests/vectors/hook_stub.cpp; an address with neither
// raises an exception a guarded check reports, or ends the run).
#ifdef EI_VECTORS_REPLAY
extern "C" void ei_replay_dispatch();
#define EI_ORIGINAL(addr) \
    { __asm { push addr } __asm { jmp ei_replay_dispatch } }
#else
extern "C" void ei_code_dispatch();
#define EI_ORIGINAL(addr) \
    { __asm { push addr } __asm { jmp ei_code_dispatch } }
#endif

// Verify build only (tests/vectors/record_*.cpp): in game.exe with EI_VECTORS=1 (the original game, the
// recorders its only hooks) route `addr` to `recorder`, and store in `orig` a trampoline running the
// original (its first `stolen` bytes, then a jump back), which the recorder calls and samples.
#define EI_RECORD(addr, recorder, stolen, orig) \
    EI_REG_RECORD(ei_reg_record_, addr, ::ei::kRegRecord); \
    static ::ei::HookRegistrar EI_CAT(ei_record_, __LINE__)( \
        addr, reinterpret_cast<void*>(&recorder), #recorder, stolen, reinterpret_cast<void**>(&orig))

// Run `fn` (void()) once in the standalone image before the registrations are taken (StartupAction).
#define EI_STARTUP_ACTION(fn) static ::ei::StartupActionRegistrar EI_CAT(ei_action_, __LINE__)(&fn, #fn)

#define EI_CAT_(a, b) a##b
#define EI_CAT(a, b) EI_CAT_(a, b)
