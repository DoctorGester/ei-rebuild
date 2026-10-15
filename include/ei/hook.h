// Function replacement framework.
//
// While the rebuild is incomplete, ei_hook.dll is loaded into the original game.exe and every
// function we have rewritten is redirected to the new code with a 5-byte JMP at its original
// address. Functions not yet rewritten are reached through EI_ORIGINAL forwarders that jump
// straight into game.exe.
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
    int stolen = 0;           // >0: build a trampoline from this many prologue bytes
    void** original = nullptr; // receives the trampoline (original function) address
    bool verify = false;      // verification wrapper: takes precedence over a plain replacement
    bool record = false;      // vector recorder (EI_RECORD): installed only in record mode, then alone
    bool recordOurs = false;  // EI_RECORD_OURS: our replacement's own entry leads to the recorder too
    bool shortFunction = false; // EI_REPLACE_SHORT / EI_REPLACE_DISPATCH: installed in the standalone image only
    bool dispatch = false;      // EI_REPLACE_DISPATCH: the replacement is a deliberate vtable dispatch
};

// Registers a replacement at static-initialisation time. Installed by InstallHooks().
struct HookRegistrar {
    HookEntry entry;
    HookRegistrar(std::uintptr_t address, void* replacement, const char* name);
    // Verification wrapper: calls both implementations and compares. `stolen` must cover whole
    // instructions with no relative operands (checked by hand from the disassembly).
    HookRegistrar(std::uintptr_t address, void* wrapper, const char* name, int stolen, void** original);
};

// Record mode (tests/vectors, EI_VECTORS=1 in the verify build): a recorder runs the original through
// its trampoline and keeps a sample of its calls as test vectors. In record mode it is the only hook
// at its address; otherwise it is not installed.
struct RecordRegistrar {
    HookRegistrar registrar;
    RecordRegistrar(std::uintptr_t address, void* recorder, const char* name, int stolen, void** original,
                    bool ours = false)
        : registrar(address, recorder, name, stolen, original) {
        registrar.entry.verify = false;
        registrar.entry.record = true;
        registrar.entry.recordOurs = ours;
    }
};

// A game function under five bytes (src/game/short_functions.cpp): too short for a jump in game.exe,
// so the hook build leaves it to the original; the standalone image, where the next function leaves
// room, jumps to the replacement.
struct ShortRegistrar {
    HookRegistrar registrar;
    ShortRegistrar(std::uintptr_t address, void* replacement, const char* name)
        : registrar(address, replacement, name) {
        registrar.entry.shortFunction = true;
    }
};

// A game function that is itself a bare vtable dispatch (MOV EAX,[ECX]; JMP [EAX+n]) to another slot:
// the replacement is compiled to the same bytes. The hook build leaves it to the original (an
// identical jump); the standalone image jumps to the replacement, and the installer checks that its
// dispatch bytes equal the original's.
struct DispatchRegistrar {
    HookRegistrar registrar;
    DispatchRegistrar(std::uintptr_t address, void* replacement, const char* name)
        : registrar(address, replacement, name) {
        registrar.entry.shortFunction = true;
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

// The harness (verify build) patches `size` bytes of game code at `address` (`what`, a string
// literal). Called before InstallHooks, it marks the functions holding the bytes as the harness's:
// their registrations are left out as expected, not as unexpected skips.
void NoteHarnessPatch(std::uintptr_t address, std::size_t size, const char* what);
// Registrations InstallHooks left out for a reason other than the harness's or another mod's patches
// (an unknown function start, a second hook on one address, ...): their rewrites never run. The
// harness fails a run with any (SV6).
int UnexpectedSkips();

// True inside ei_standalone.exe (src/standalone/host.cpp, docs/notes/standalone.md section 2): no
// game.exe code, only its headers and data laid out at kImageBase, into which InstallHooks writes the
// jumps (plain replacements only). standalone.cpp.
bool Standalone();
// The game.exe file the standalone host built the image from.
const char* StandaloneExePath();
// Called with a line naming it when the standalone image reaches game code that has no rewrite (an INT3
// of the blank code section), before the process ends: the harness (verify build) puts it in its report.
void SetMissingCodeHandler(void (*handler)(const char* what));

// A check that runs once, right after the hooks are installed (the entry of WinMain). For code that
// only runs before that, the game's static initialisers: what they built is still there, and the
// check compares it with what the port builds. Verification build only (tests/verify).
struct StartupCheck {
    void (*run)();
    const char* name;
    StartupCheck* next;
};
struct StartupCheckRegistrar {
    StartupCheck entry;
    StartupCheckRegistrar(void (*run)(), const char* name);
};

// Code that runs once, in every build, right before the hooks are installed: the Rebirth flags are
// known and other mods' patches are in place, nothing is hooked yet (the standalone build runs it
// at startup). For data a Rebirth patch rewrites before WinMain (the widescreen layout widths).
// Not run with EI_NO_HOOKS.
struct StartupAction {
    void (*run)();
    const char* name;
    StartupAction* next;
};
struct StartupActionRegistrar {
    StartupAction entry;
    StartupActionRegistrar(void (*run)(), const char* name);
};

// Arranges for InstallHooks() to run when game.exe's WinMain is entered (after other mods patch).
void InstallHooksAtWinMain();

} // namespace ei

// Replace the function at `addr` with a free function or static function.
#define EI_REPLACE(addr, fn) \
    static ::ei::HookRegistrar EI_CAT(ei_hook_, __LINE__)(addr, reinterpret_cast<void*>(&fn), #fn)

// Replace the method at `addr` with a C++ member function.
#define EI_REPLACE_METHOD(addr, method) \
    static ::ei::HookRegistrar EI_CAT(ei_hook_, __LINE__)(addr, ::ei::MemberAddress(&method), #method)

// Register a method whose body is a tail call through the vtable, the same dispatch as the original's
// own (DestroyEffect, SpecRemove, SpecRefresh); see DispatchRegistrar. Standalone image only.
#define EI_REPLACE_DISPATCH(addr, method) \
    static ::ei::DispatchRegistrar EI_CAT(ei_dispatch_, __LINE__)(addr, ::ei::MemberAddress(&method), #method)

// Replace the function under five bytes at `addr` in the standalone image only (ShortRegistrar).
#define EI_REPLACE_SHORT(addr, fn) \
    static ::ei::ShortRegistrar EI_CAT(ei_short_, __LINE__)(addr, reinterpret_cast<void*>(&fn), #fn)

// Define a function body that forwards to original code not rewritten yet. Works for any calling
// convention (including __thiscall methods) because it is a bare jump with the stack untouched.
#define EI_ORIGINAL(addr) \
    { __asm { mov eax, addr } __asm { jmp eax } }

// Verification build only (tests/verify_*.cpp): route `addr` to `wrapper`, and store a callable
// pointer to the original code in `orig`.
#define EI_VERIFY(addr, wrapper, stolen, orig)     static ::ei::HookRegistrar EI_CAT(ei_verify_, __LINE__)(         addr, reinterpret_cast<void*>(&wrapper), #wrapper, stolen, reinterpret_cast<void**>(&orig))

// Verification build only (tests/vectors/record_*.cpp): in record mode route `addr` to `recorder`,
// which calls the original through `orig` (stolen bytes as for EI_VERIFY).
#define EI_RECORD(addr, recorder, stolen, orig)     static ::ei::RecordRegistrar EI_CAT(ei_record_, __LINE__)(         addr, reinterpret_cast<void*>(&recorder), #recorder, stolen, reinterpret_cast<void**>(&orig))

// EI_RECORD for a function whose callers are rewritten (they call our replacement directly, never the
// address): in record mode our replacement's entry also jumps to the recorder, so their calls run the
// original and are recorded too. Only for replacements of at least 5 bytes not shared with another
// function.
#define EI_RECORD_OURS(addr, recorder, stolen, orig)     static ::ei::RecordRegistrar EI_CAT(ei_record_, __LINE__)(         addr, reinterpret_cast<void*>(&recorder), #recorder, stolen, reinterpret_cast<void**>(&orig), true)

// Verification build only: run `fn` (void()) once when the hooks have been installed.
#define EI_STARTUP_CHECK(fn) static ::ei::StartupCheckRegistrar EI_CAT(ei_startup_, __LINE__)(&fn, #fn)

// Run `fn` (void()) once before the hooks are installed, in every build (StartupAction).
#define EI_STARTUP_ACTION(fn) static ::ei::StartupActionRegistrar EI_CAT(ei_action_, __LINE__)(&fn, #fn)

#define EI_CAT_(a, b) a##b
#define EI_CAT(a, b) EI_CAT_(a, b)
