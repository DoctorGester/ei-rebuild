// Coverage of chosen game addresses (EI_COVER): which of them a run executes, for picking the
// scenarios that need a Rebirth run (tools/rebirth_coverage.py). Off unless the environment names a
// file of addresses (first hex word of each line, '#' comments).
//
// Each address gets a one-shot INT3 after the hooks are installed; the first time it runs, a vectored
// handler puts the byte back, logs "cover <address>" and resumes at the address. A hooked address
// gets its INT3 on our JMP, so game code calling the address is seen, and so are EI_ORIGINAL
// forwarders; rewritten code that calls a rewrite directly is not.
#include "log.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

namespace ei {

namespace {

std::unordered_map<std::uintptr_t, unsigned char> g_saved;   // address -> its first byte
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
        PutByte(address, it->second);
        Log("cover %08X", unsigned(address));
    }
    info->ContextRecord->Eip = static_cast<DWORD>(address);
    return EXCEPTION_CONTINUE_EXECUTION;
}

} // namespace

void InstallCoverage() {
    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("EI_COVER", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
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
        if (end == line || address < 0x00401000 || address >= 0x00740000) continue;
        g_saved.emplace(address, *reinterpret_cast<unsigned char*>(address));
    }
    std::fclose(f);
    AddVectoredExceptionHandler(1, OnBreakpoint);
    for (auto& [address, byte] : g_saved) PutByte(address, 0xCC);
    Log("cover: %u address(es) from %s", unsigned(g_saved.size()), path);
}

} // namespace ei
