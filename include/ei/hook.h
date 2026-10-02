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
};

// Registers a replacement at static-initialisation time. Installed by InstallHooks().
struct HookRegistrar {
    HookEntry entry;
    HookRegistrar(std::uintptr_t address, void* replacement, const char* name);
    // Verification wrapper: calls both implementations and compares. `stolen` must cover whole
    // instructions with no relative operands (checked by hand from the disassembly).
    HookRegistrar(std::uintptr_t address, void* wrapper, const char* name, int stolen, void** original);
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

} // namespace ei

// Replace the function at `addr` with a free function or static function.
#define EI_REPLACE(addr, fn) \
    static ::ei::HookRegistrar EI_CAT(ei_hook_, __LINE__)(addr, reinterpret_cast<void*>(&fn), #fn)

// Replace the method at `addr` with a C++ member function.
#define EI_REPLACE_METHOD(addr, method) \
    static ::ei::HookRegistrar EI_CAT(ei_hook_, __LINE__)(addr, ::ei::MemberAddress(&method), #method)

// Define a function body that forwards to original code not rewritten yet. Works for any calling
// convention (including __thiscall methods) because it is a bare jump with the stack untouched.
#define EI_ORIGINAL(addr) \
    { __asm { mov eax, addr } __asm { jmp eax } }

// Verification build only (tests/verify_*.cpp): route `addr` to `wrapper`, and store a callable
// pointer to the original code in `orig`.
#define EI_VERIFY(addr, wrapper, stolen, orig)     static ::ei::HookRegistrar EI_CAT(ei_verify_, __LINE__)(         addr, reinterpret_cast<void*>(&wrapper), #wrapper, stolen, reinterpret_cast<void**>(&orig))

#define EI_CAT_(a, b) a##b
#define EI_CAT(a, b) EI_CAT_(a, b)
