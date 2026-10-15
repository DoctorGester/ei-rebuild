// Coverage of chosen game addresses (EI_COVER): which of them a run executes. Off unless the
// environment names a file of addresses (first hex word of each line, '#' comments; for picking the
// scenarios that need a Rebirth run, tools/rebirth_coverage.py), or is `hooks`: every address the
// hooks were installed at, for the coverage list of the rewritten functions (tools/coverage.py).
//
// Each address gets a one-shot INT3 after the hooks are installed; the first time it runs, a vectored
// handler puts the byte back, logs "cover <address>" and resumes at the address. A hooked address
// gets its INT3 on our JMP, so game code calling the address is seen, and so are EI_ORIGINAL
// forwarders. With `hooks` the first byte of each plain replacement (our function) gets one too and
// logs its game address, so our code calling a rewrite directly is seen as well (not where the
// compiler inlined the call).
#include "log.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ei {

const std::vector<std::pair<std::uintptr_t, void*>>& InstalledHooks();   // hook.cpp

namespace {

struct Watched {
    unsigned char byte;                      // the code's first byte
    std::vector<std::uintptr_t> addresses;   // the game addresses logged when it runs
};
std::unordered_map<std::uintptr_t, Watched> g_saved;   // code address -> what it watches
std::mutex g_lock;

void PutByte(std::uintptr_t address, unsigned char value) {
    auto* p = reinterpret_cast<unsigned char*>(address);
    DWORD old;
    VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old);
    *p = value;
    VirtualProtect(p, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 1);
}

LONG CALLBACK OnBreakpoint(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    const auto address = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    std::lock_guard<std::mutex> hold(g_lock);
    auto it = g_saved.find(address);
    if (it == g_saved.end()) return EXCEPTION_CONTINUE_SEARCH;
    if (*reinterpret_cast<unsigned char*>(address) == 0xCC) {   // a second thread may have arrived first
        PutByte(address, it->second.byte);
        for (std::uintptr_t game : it->second.addresses) Log("cover %08X", unsigned(game));
    }
    info->ContextRecord->Eip = static_cast<DWORD>(address);
    return EXCEPTION_CONTINUE_EXECUTION;
}

void Watch(std::uintptr_t code, std::uintptr_t game) {
    auto [it, added] = g_saved.try_emplace(code);
    if (added) it->second.byte = *reinterpret_cast<unsigned char*>(code);
    it->second.addresses.push_back(game);
}

bool InImage(unsigned long address) { return address >= 0x00401000 && address < 0x00740000; }

} // namespace

void InstallCoverage() {
    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("EI_COVER", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    if (std::strcmp(path, "hooks") == 0) {
        for (const auto& [address, replacement] : InstalledHooks()) {
            if (!InImage(static_cast<unsigned long>(address))) continue;
            Watch(address, address);
            if (replacement) Watch(reinterpret_cast<std::uintptr_t>(replacement), address);
        }
    } else {
        FILE* f = nullptr;
        if (fopen_s(&f, path, "r") != 0 || !f) {
            Log("cover: cannot read %s", path);
            return;
        }
        char line[256];
        while (std::fgets(line, sizeof(line), f)) {
            if (line[0] == '#') continue;
            char* end = nullptr;
            unsigned long address = std::strtoul(line, &end, 16);
            if (end != line && InImage(address)) Watch(address, address);
        }
        std::fclose(f);
    }
    AddVectoredExceptionHandler(1, OnBreakpoint);
    for (auto& [address, watched] : g_saved) PutByte(address, 0xCC);
    Log("cover: %u address(es) from %s", unsigned(g_saved.size()), path);
}

} // namespace ei
