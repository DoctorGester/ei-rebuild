// Sampling profiler for the main thread (EI_PROFILE=1): about once a millisecond the thread is
// suspended and its EIP and the return addresses on its stack are written to profile.txt in the
// output folder. tools/profile_report.py turns that into time per function between harness steps.
// The stack is scanned, not walked (the game's code omits frame pointers), so a stale return address
// can show up as a caller; only words that follow a CALL instruction in game.exe or ei_hook count.
#include "log.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "psapi.lib")

namespace ei {

void OutputPath(const char* name, char* out);   // dllmain.cpp

namespace {

HANDLE g_main = nullptr;
std::uintptr_t g_stackBase = 0;
std::uintptr_t g_selfBase = 0, g_selfEnd = 0, g_exeEnd = 0;

bool InCode(std::uintptr_t a) { return (a >= 0x00401000 && a < g_exeEnd) || (a >= g_selfBase + 0x1000 && a < g_selfEnd); }

// A return address: the bytes before it are a CALL (E8 rel32, or FF /2 in its 2-7 byte forms).
bool AfterCall(std::uintptr_t a) {
    const auto* p = reinterpret_cast<const unsigned char*>(a);
    if (p[-5] == 0xE8) return true;
    for (int n = 2; n <= 7; ++n)
        if (p[-n] == 0xFF && ((p[-n + 1] >> 3) & 7) == 2) return true;
    return false;
}

void WriteModules(FILE* f, std::vector<HMODULE>& known) {
    HMODULE modules[512];
    DWORD bytes = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof modules, &bytes)) return;
    for (DWORD i = 0; i < bytes / sizeof(HMODULE) && i < 512; ++i) {
        bool seen = false;
        for (HMODULE m : known) seen = seen || m == modules[i];
        if (seen) continue;
        known.push_back(modules[i]);
        MODULEINFO info{};
        char path[MAX_PATH] = {};
        GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof info);
        GetModuleFileNameA(modules[i], path, MAX_PATH);
        std::fprintf(f, "M %08X %08X %s\n", unsigned(reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll)), unsigned(info.SizeOfImage), path);
    }
}

DWORD WINAPI Sampler(void*) {
    char path[MAX_PATH];
    OutputPath("profile.txt", path);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") || !f) return 0;
    timeBeginPeriod(1);
    LARGE_INTEGER frequency, start, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(f, "T %02d:%02d:%02d.%03d\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    std::vector<HMODULE> known;
    WriteModules(f, known);
    std::vector<std::uint32_t> stack(16384);
    for (unsigned sample = 1;; ++sample) {
        Sleep(1);
        if (SuspendThread(g_main) == DWORD(-1)) break;
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(g_main, &context)) {
            ResumeThread(g_main);
            break;
        }
        std::size_t words = 0;
        if (context.Esp < g_stackBase) {
            words = std::min<std::size_t>((g_stackBase - context.Esp) / 4, stack.size());
            std::memcpy(stack.data(), reinterpret_cast<const void*>(context.Esp), words * 4);
        }
        ResumeThread(g_main);
        QueryPerformanceCounter(&now);
        std::fprintf(f, "S %.2f %08X", double(now.QuadPart - start.QuadPart) * 1000.0 / double(frequency.QuadPart), unsigned(context.Eip));
        int callers = 0;
        for (std::size_t i = 0; i < words && callers < 96; ++i)
            if (InCode(stack[i]) && AfterCall(stack[i])) {
                std::fprintf(f, " %X", unsigned(stack[i]));
                ++callers;
            }
        std::fputc('\n', f);
        if (sample % 2000 == 0) {
            WriteModules(f, known);
            std::fflush(f);
        }
    }
    std::fclose(f);
    return 0;
}

} // namespace

// From WinMain (the main thread).
void InstallProfiler() {
    char value[4];
    if (!GetEnvironmentVariableA("EI_PROFILE", value, sizeof value) || value[0] != '1') return;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_main, 0, FALSE, DUPLICATE_SAME_ACCESS);
    g_stackBase = reinterpret_cast<std::uintptr_t>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
    MODULEINFO info{};
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&InstallProfiler), &self);
    GetModuleInformation(GetCurrentProcess(), self, &info, sizeof info);
    g_selfBase = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
    g_selfEnd = g_selfBase + info.SizeOfImage;
    GetModuleInformation(GetCurrentProcess(), GetModuleHandleA(nullptr), &info, sizeof info);
    g_exeEnd = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll) + info.SizeOfImage;
    HANDLE thread = CreateThread(nullptr, 0, Sampler, nullptr, 0, nullptr);
    SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
    CloseHandle(thread);
    Log("profile: sampling the main thread into profile.txt");
}

} // namespace ei
