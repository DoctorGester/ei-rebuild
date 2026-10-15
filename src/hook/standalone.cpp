// ei_hook.dll inside ei_standalone.exe (src/standalone/host.cpp; docs/notes/standalone.md section 2).
// The host has laid out game.exe's headers and data at 0x400000 with INT3 in place of its code and
// resolved its imports; here the jumps to our code are written and checked, and WinMain runs.
#include "ei/hook.h"
#include "log.h"
#include "standalone/static_init.h"
#include "game/originals_library.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ei {

const std::vector<std::pair<std::uintptr_t, void*>>& InstalledHooks();   // hook.cpp
std::uintptr_t FunctionContaining(std::uintptr_t address);              // hook.cpp

namespace {

char g_exePath[MAX_PATH];
void (*g_missingCodeHandler)(const char* what);

// game.exe's code section (.text, 0x401000-0x73A670).
constexpr std::uintptr_t kTextBegin = 0x00401000;
constexpr std::uintptr_t kTextEnd = 0x0073A670;
constexpr std::uintptr_t kWinMain = 0x00456440;

// A code address as the log names it: inside game.exe's code plainly, elsewhere as module+offset.
void DescribeCode(std::uintptr_t address, char* out, std::size_t size) {
    HMODULE module = nullptr;
    char path[MAX_PATH];
    if ((address < kTextBegin || address >= kTextEnd) &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module) &&
        GetModuleFileNameA(module, path, MAX_PATH)) {
        // "ei_hook+offset", as the harness's crash log has it (tools/resolve_crash.py).
        const char* slash = std::strrchr(path, '\\');
        const char* name = slash ? slash + 1 : path;
        const char* dot = std::strrchr(name, '.');
        std::snprintf(out, size, "%08X (%.*s+%X)", static_cast<unsigned>(address),
                      static_cast<int>(dot ? dot - name : std::strlen(name)), name,
                      static_cast<unsigned>(address - reinterpret_cast<std::uintptr_t>(module)));
        return;
    }
    std::snprintf(out, size, "%08X", static_cast<unsigned>(address));
}

// Execution reached an address of game.exe's code with no rewrite: the INT3 filling the image there.
// Its caller is the return address on the stack when the hit is a function's first byte (a call).
LONG CALLBACK OnBreakpoint(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    const auto at = reinterpret_cast<std::uintptr_t>(r->ExceptionAddress);
    if (r->ExceptionCode != EXCEPTION_BREAKPOINT || at < kTextBegin || at >= kTextEnd) return EXCEPTION_CONTINUE_SEARCH;
    const std::uintptr_t function = FunctionContaining(at);
    char where[64], caller[160], what[256];
    if (function) std::snprintf(where, sizeof(where), "in the game function at %08X", static_cast<unsigned>(function));
    else strcpy_s(where, "library code");
    DescribeCode(*reinterpret_cast<const std::uint32_t*>(e->ContextRecord->Esp), caller, sizeof(caller));
    std::snprintf(what, sizeof(what), "no code at %08X (%s), [esp] %s", static_cast<unsigned>(at), where, caller);
    Log("standalone: %s", what);
    if (g_missingCodeHandler) g_missingCodeHandler(what);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* e) {
    Log("standalone: unhandled exception %08lX at %p", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress);
    TerminateProcess(GetCurrentProcess(), 4);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Every jump InstallHooks wrote leads into this DLL, to the registered replacement where there is one.
// An address registered twice keeps the jump installed last (as in the hook build): logged, not counted.
int CheckJumps() {
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&CheckJumps), &self);
    std::unordered_map<std::uintptr_t, int> times;
    for (const auto& installed : InstalledHooks()) ++times[installed.first];
    int bad = 0;
    for (const auto& [address, registered] : InstalledHooks()) {
        int& n = times[address];
        if (n > 1) Log("standalone: %08X is registered %d times; the last one installed runs", static_cast<unsigned>(address), n);
        const void* replacement = n == 1 ? registered : nullptr;
        n = 0;
        const auto* code = reinterpret_cast<const unsigned char*>(address);
        std::int32_t rel;
        std::memcpy(&rel, code + 1, 4);
        const std::uintptr_t target = address + 5 + rel;
        HMODULE owner = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(target), &owner);
        if (code[0] != 0xE9 || owner != self || (replacement && target != reinterpret_cast<std::uintptr_t>(replacement))) {
            Log("standalone: bad jump at %08X", static_cast<unsigned>(address));
            ++bad;
        }
    }
    return bad;
}

} // namespace

bool Standalone() { return GetModuleHandleA(nullptr) != reinterpret_cast<HMODULE>(kImageBase); }

const char* StandaloneExePath() { return g_exePath; }

void SetMissingCodeHandler(void (*handler)(const char* what)) { g_missingCodeHandler = handler; }

} // namespace ei

// Called by the host once the image is laid out. `run` 0: install and check the jumps, then return
// (0 when every jump is right and WinMain has one). `run` 1: then run WinMain.
extern "C" __declspec(dllexport) int __cdecl EiStandaloneMain(const char* exePath, int run) {
    using namespace ei;
    strcpy_s(g_exePath, exePath);
    SetProcessDPIAware();
    const int installed = InstallHooks();
    const int bad = CheckJumps();
    const bool winMain = *reinterpret_cast<const unsigned char*>(kWinMain) == 0xE9;
    Log("standalone: %d jump(s) written into the image, %d wrong, WinMain %s", installed, bad,
        winMain ? "rewritten" : "missing");
    // What the static initialisers call that has no code yet (logged; not a failure of the check).
    static_init::ReportMissingTargets();
    if (!run) return installed > 0 && bad == 0 && winMain ? 0 : 1;
    AddVectoredExceptionHandler(1, OnBreakpoint);
    SetUnhandledExceptionFilter(OnUnhandled);
    // As the CRT's start-up: the static initialisers, WinMain, then exit with its result (the CRT's
    // exit runs the functions the initialisers registered with atexit).
    static_init::RunStaticInitialisers();
    using WinMainFn = int(__stdcall*)(HINSTANCE, HINSTANCE, char*, int);
    char commandLine[1] = {};
    const int result =
        reinterpret_cast<WinMainFn>(kWinMain)(reinterpret_cast<HINSTANCE>(kImageBase), nullptr, commandLine, SW_SHOWNORMAL);
    ExitGame(result);
    return result;
}
