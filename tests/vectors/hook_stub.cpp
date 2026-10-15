// ei_vectors.exe links our game sources without the hook framework: their EI_REPLACE registrations
// install nothing here, but say where a forwarder (EI_ORIGINAL) to the same address goes on to.
#include "ei/hook.h"
#include "vectors.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace ei {

namespace {

struct Target {
    std::uintptr_t address;
    void* to;
    const char* name;
};

// Filled during static initialisation (function-local: the registrars of other files run first).
std::vector<Target>& Rewrites() {
    static std::vector<Target> targets;
    return targets;
}
std::vector<Target>& StandIns() {
    static std::vector<Target> targets;
    return targets;
}

const Target* Find(std::vector<Target>& targets, std::uintptr_t address) {
    for (const Target& t : targets)
        if (t.address == address) return &t;
    return nullptr;
}

} // namespace

HookRegistrar::HookRegistrar(std::uintptr_t address, void* replacement, const char* name)
    : entry{address, replacement, name, nullptr} {
    Rewrites().push_back({address, replacement, name});
}

HookRegistrar::HookRegistrar(std::uintptr_t address, void* wrapper, const char* name, int stolen, void** original)
    : entry{address, wrapper, name, nullptr, stolen, original, true} {}

// ei_vectors runs without game.exe: code that would write the exe's own runtime data in the hook build
// (the game CRT's errno, libcrt::WideToBytes) leaves it alone, as in the standalone image.
bool Standalone() { return true; }

} // namespace ei

void ei::vectors::ReplayStandIn(std::uintptr_t address, void* function, const char* name) {
    StandIns().push_back({address, function, name});
}

// Where the forwarder to `address` goes: a stand-in first, then the rewrite.
extern "C" void* __cdecl ei_replay_forward_target(std::uintptr_t address) {
    if (const auto* t = ei::Find(ei::StandIns(), address)) return t->to;
    if (const auto* t = ei::Find(ei::Rewrites(), address)) return t->to;
    std::printf("ei_vectors: a forwarder to %08X was called; nothing in ei_vectors is registered there\n",
                static_cast<unsigned>(address));
    std::fflush(stdout);
    std::abort();
}

// Entered by a jump from a forwarder, which pushed its address over the caller's return address.
// Every register (EAX too: some originals take arguments in it) and the stack are as at the
// forwarder's entry when the target is reached.
extern "C" __declspec(naked) void ei_replay_forward() {
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [esp + 12]
        call ei_replay_forward_target
        add esp, 4
        mov [esp + 12], eax
        pop edx
        pop ecx
        pop eax
        ret
    }
}
