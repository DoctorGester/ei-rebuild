// ei_standalone: runs the rebuilt game without game.exe (docs/notes/standalone.md section 2).
//
// The host is an ordinary exe (S18k): it neither reads game.exe nor lays out anything at game.exe's addresses.
// game.exe's resources are its own (S18j: built in from game.exe by tools/gen_resources.py at build time, and the
// game's hInstance is this module); ei_hook.dll keeps game.exe's .rdata and .data in its own storage with bytes from
// the build (src/standalone/image_data.cpp), resolves game.exe's imports into a table of its own as it initialises
// (image_imports.cpp), takes the registrations as game.exe's code (InstallHooks, image_code.cpp), points the code
// cells of game.exe's data at them and runs WinMain. Forwarders, function pointers in the data and vtable slots reach
// our code that way.
//
// The guard: first thing, the host reserves what of game.exe's range (0x400000 up to its SizeOfImage) is free, with
// no access, so nothing is allocated there later and a stray game address (a jump into game.exe's code that went
// around CodeAddress, a read of its data that missed the seam) faults and is named instead of reaching other memory.
// What the loader had already put there (this exe, a heap) stays as it is; ei_hook.dll logs how much is guarded.
//
// usage: ei_standalone [--dll <ei_hook.dll>] [--run] [-- <game arguments>]
//   Run from the game folder (the working directory the game expects). Without --run the host builds
//   the image, takes the registrations, checks them and exits: no window, nothing of the game runs. What
//   follows "--" is the game's command line, as typed (WinMain's, and GetCommandLineA's after the exe).

#include "game/game_data.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// game.exe's range in its own process and its parts: the headers' page, .text, .rdata and .data (the moved range),
// .rsrc.
using ei::kImageBegin;
using ei::kImageEnd;
constexpr std::uintptr_t kCodeBegin = kImageBegin + 0x1000;

// Reserves, with no access, every free part of game.exe's range (each from its first allocation granule: the
// rest of a granule after another allocation cannot be allocated anyway, and is free memory, which faults too).
void ReserveGameRange() {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const std::uintptr_t granule = info.dwAllocationGranularity;
    for (std::uintptr_t a = kImageBegin; a < kImageEnd;) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<const void*>(a), &mbi, sizeof(mbi))) break;
        const std::uintptr_t end = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (mbi.State == MEM_FREE) {
            const std::uintptr_t from = (a + granule - 1) & ~(granule - 1);
            const std::uintptr_t to = end < kImageEnd ? end : kImageEnd;
            if (from < to) VirtualAlloc(reinterpret_cast<void*>(from), to - from, MEM_RESERVE, PAGE_NOACCESS);
        }
        a = end;
    }
}

// An access to game.exe's range before ei_hook.dll's own reports are in place (its static constructors run at
// LoadLibrary), or to a part they leave to the host (the headers, .rsrc): named on stderr (ei_launch's run notes)
// and the process ends, never silently (S16c). ei_hook.dll's handlers, added later at the front, report the code
// section and the data themselves.
LONG CALLBACK OnGameRange(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    const std::uintptr_t data = r->ExceptionInformation[1];
    if (data - kImageBegin >= kImageEnd - kImageBegin) return EXCEPTION_CONTINUE_SEARCH;
    const char* part = data < kCodeBegin ? "headers" : data < ei::kMovedBegin ? "code" : data < ei::kMovedEnd ? "data" : ".rsrc";
    if (r->ExceptionInformation[0] == 8 || reinterpret_cast<std::uintptr_t>(r->ExceptionAddress) == data)
        std::fprintf(stderr, "ei_standalone: no code at %08X before ei_hook.dll's missing-code report, [esp] %08X\n",
                     static_cast<unsigned>(data), static_cast<unsigned>(*reinterpret_cast<const std::uint32_t*>(e->ContextRecord->Esp)));
    else
        std::fprintf(stderr, "ei_standalone: %s of game.exe's %s at %08X by %08X: the image has nothing of game.exe there\n",
                     r->ExceptionInformation[0] == 1 ? "write" : "read", part, static_cast<unsigned>(data),
                     static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(r->ExceptionAddress)));
    std::fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

int Fail(const char* what) {
    std::fprintf(stderr, "ei_standalone: %s (error %lu)\n", what, GetLastError());
    return 1;
}

std::string Folder(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// The game's arguments: what follows the token "--" on the host's command line, as typed.
std::string GameArguments() {
    const char* p = GetCommandLineA();
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* token = p;
        bool quoted = false;
        for (; *p && (quoted || (*p != ' ' && *p != '\t')); ++p)
            if (*p == '"') quoted = !quoted;
        if (p - token == 2 && token[0] == '-' && token[1] == '-') {
            while (*p == ' ' || *p == '\t') ++p;
            return p;
        }
    }
    return {};
}

} // namespace

// Marks this process as the standalone image for ei_hook.dll (ei::Standalone): game.exe exports nothing.
extern "C" __declspec(dllexport) int EiStandaloneHost = 1;

int main(int argc, char** argv) {
    ReserveGameRange();
    AddVectoredExceptionHandler(1, OnGameRange);
    // Under ei_launch --standalone stdout is a pipe: unbuffered, so a run that ends on a missing-code report
    // (TerminateProcess) loses none of it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string dll = Folder(self) + "\\ei_hook.dll";
    bool run = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--dll") && i + 1 < argc) dll = argv[++i];
        else if (!std::strcmp(argv[i], "--run")) run = true;
        else if (!std::strcmp(argv[i], "--")) break;
        else {
            std::fprintf(stderr, "usage: ei_standalone [--dll <ei_hook.dll>] [--run] [-- <game arguments>]\n");
            return 2;
        }
    }
    HMODULE hook = LoadLibraryA(dll.c_str());
    if (!hook) return Fail(("cannot load " + dll).c_str());
    using Main = int(__cdecl*)(int run, const char* arguments);
    auto start = reinterpret_cast<Main>(GetProcAddress(hook, "EiStandaloneMain"));
    if (!start) return Fail("ei_hook.dll has no EiStandaloneMain");
    const int result = start(run ? 1 : 0, GameArguments().c_str());
    std::printf("ei_standalone: %s %d\n", run ? "the game returned" : "check", result);
    return result;
}
