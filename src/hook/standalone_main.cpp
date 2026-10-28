// ei_hook.dll inside ei_standalone.exe (src/standalone/host.cpp; docs/notes/standalone.md section 2): the image's
// start-up, which only the image runs (the run's kind and what both runs use: standalone.cpp).
// The host is an ordinary exe that reads no game.exe and keeps what of game.exe's range is free reserved with no
// access; the DLL's static
// initialisation has put .rdata, .data and the import slots in its own storage (image_data.cpp,
// image_imports.cpp); here the start-up actions run, then the static initialisers and WinMain (app::GameMain).
#include "ei/hook.h"
#include "log.h"
#include "game/app.h"
#include "standalone/static_init.h"
#include "game/game_data.h"
#include "game/settings_file.h"
#include "game/system_text.h"
#include "lib/crt.h"
#include "render/render.h"

#include <mbctype.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace ei {

int ImageImportsMissing();   // standalone/image_imports.cpp


// A code address as the log names it (standalone.cpp).
void DescribeCode(std::uintptr_t address, char* out, std::size_t size);

// A failure of the image (no code at an address, a use of moved data): logged, and handed to the
// harness's handler (its report) before the process ends.
void ReportImageFailure(const char* what);

// Execution reached game.exe's code at `address` with no code of ours for it (the guarded code section, a compiled
// table's unported slot): "no code at <address>, [esp] <caller>" (tools/resolve_crash.py reads it), then the process
// ends.
[[noreturn]] void ReportNoCode(std::uintptr_t address, std::uintptr_t caller) {
    char from[160], what[256];
    DescribeCode(caller, from, sizeof(from));
    std::snprintf(what, sizeof(what), "no code at %08X, [esp] %s", static_cast<unsigned>(address), from);
    ReportImageFailure(what);
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) Sleep(INFINITE);
}

namespace {

std::string g_commandLine;      // as the CRT would see game.exe's: "<game.exe>" and the game's arguments
std::size_t g_argumentsAt = 0;  // where the arguments start in it (WinMain's command line)


// An access to game.exe's guarded code section: execution there (a jump or call to an address of game.exe's
// code: its caller is the return address on the stack when it is a call) is
// reported as no code at the address; a read or write of its bytes as such.
LONG CALLBACK OnOldCode(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    const auto at = reinterpret_cast<std::uintptr_t>(r->ExceptionAddress);
    const std::uintptr_t data = r->ExceptionInformation[1];
    const bool execute = r->ExceptionInformation[0] == 8 || at == data;
    if (!InGameText(data)) return EXCEPTION_CONTINUE_SEARCH;
    if (execute) ReportNoCode(at, *reinterpret_cast<const std::uint32_t*>(e->ContextRecord->Esp));
    char code[160], what[320];
    DescribeCode(at, code, sizeof(code));
    std::snprintf(what, sizeof(what), "%s of game.exe's code at %08X by %s: the image's code section is guarded",
                  r->ExceptionInformation[0] == 1 ? "write" : "read", static_cast<unsigned>(data), code);
    std::fprintf(stderr, "ei_hook: %s\n", what);
    std::fflush(stderr);
    ReportImageFailure(what);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* e) {
    Log("standalone: unhandled exception %08lX at %p", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress);
    TerminateProcess(GetCurrentProcess(), 4);
    return EXCEPTION_CONTINUE_SEARCH;
}

// The image has nothing of game.exe at its addresses: no instruction of the original runs, none that calls
// through game.exe's import slots (CALL [slot], JMP [slot]) either. The host reserved what of game.exe's range was
// free with no access, so a stray game address faults and is named (OnOldCode, image_data.cpp's OnOldData, the
// host's OnGameRange). Logs what of the range an access would not fault on (memory the loader had put there before
// the host ran) and returns its size.
std::size_t CheckGuard() {
    std::size_t open = 0;
    for (std::uintptr_t a = kImageBegin; a < kImageEnd;) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<const void*>(a), &mbi, sizeof(mbi))) return kImageEnd - a + open;
        const std::uintptr_t regionEnd = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        const std::uintptr_t end = (std::min)(kImageEnd, regionEnd);
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            char owner[MAX_PATH] = "not a module";
            if (mbi.Type == MEM_IMAGE)
                GetModuleFileNameA(static_cast<HMODULE>(mbi.AllocationBase), owner, MAX_PATH);
            Log("standalone: %08X-%08X of game.exe's range is open (%s)", static_cast<unsigned>(a),
                static_cast<unsigned>(end), owner);
            open += end - a;
        }
        a = end;
    }
    return open;
}

// ---- game.exe's module, as the image's code sees it ------------------------------------------------
// The game's module is the host's: WinMain's hInstance and GetModuleHandleA(NULL) are ei_standalone.exe, which
// has none of game.exe's resources (the strings are literals in our code), so DirectInput and the window class
// take the handle as it is, and GetModuleFileNameA(NULL) names the host. Only the command line is routed: the
// game's arguments after game.exe's path, as the original's.

LPSTR WINAPI ImageCommandLine() { return g_commandLine.data(); }

// The image's import slots that lead to a wrapper here instead of their export (ImageRoute).
struct Route {
    const char* dll;
    const char* function;
    const void* replacement;
};
constexpr int kRoutes = 1;
bool g_routed[kRoutes];   // zero before any static constructor: ImageRoute runs from image_imports.cpp's

// Points `base`'s import slots of `dll`!`function` at `replacement` (another module's import table: Miles's).
// Slots are matched by what they hold (the export the loader resolved), so a module without an import name
// table works too.
bool RouteImport(std::uintptr_t base, const char* dll, const char* function, const void* replacement) {
    HMODULE exporter = GetModuleHandleA(dll);
    const FARPROC current = exporter ? GetProcAddress(exporter, function) : nullptr;
    if (!current) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    bool routed = false;
    for (auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
        if (_stricmp(reinterpret_cast<const char*>(base + d->Name), dll) != 0) continue;
        for (auto* slot = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + d->FirstThunk); slot->u1.Function; ++slot) {
            if (slot->u1.Function != reinterpret_cast<DWORD>(current)) continue;
            DWORD old;
            VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
            slot->u1.Function = reinterpret_cast<DWORD>(replacement);
            VirtualProtect(slot, sizeof(*slot), old, &old);
            routed = true;
        }
    }
    return routed;
}

// Miles (mss32.dll) loads every *.asi it finds as a sound provider. EI_Plugin.asi (Rebirth) patches
// game.exe's code, which the image does not have: it stays out (its features are Rebirth
// flags of our code, from ei_rebirth.ini). mp3dec.asi, the MP3 decoder, loads.
HMODULE WINAPI MilesLoadLibrary(LPCSTR name) {
    const char* slash = name ? std::strrchr(name, '\\') : nullptr;
    const char* file = slash ? slash + 1 : name;
    if (file && _stricmp(file, "EI_Plugin.asi") == 0) {
        Log("standalone: Miles asked for %s: not loaded into the image", name);
        SetLastError(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    return LoadLibraryA(name);
}

// The image's command-line import and the wrapper that takes its slot.
const Route* Routes() {
    static const Route routes[kRoutes] = {
        {"KERNEL32.dll", "GetCommandLineA", reinterpret_cast<const void*>(&ImageCommandLine)},
    };
    return routes;
}

// Checks that ImageRoute took every route and routes Miles's LoadLibraryA. False when one of them is not so.
bool RouteImageCalls() {
    bool ok = true;
    for (int i = 0; i < kRoutes; ++i) {
        if (g_routed[i]) continue;
        Log("standalone: game.exe's import %s!%s not found to route", Routes()[i].dll, Routes()[i].function);
        ok = false;
    }
    if (HMODULE miles = GetModuleHandleA("mss32.dll")) {
        if (!RouteImport(reinterpret_cast<std::uintptr_t>(miles), "KERNEL32.dll", "LoadLibraryA",
                         reinterpret_cast<const void*>(&MilesLoadLibrary))) {
            Log("standalone: mss32.dll's LoadLibraryA not found: EI_Plugin.asi would load");
            ok = false;
        }
    }
    return ok;
}

} // namespace

// The wrapper image_imports.cpp puts in the image's slot of `dll`!`function` instead of its export, or nullptr.
// Called during ei_hook.dll's static initialisation, before this file's: the table is a function's static (made
// at its first call), the flags are zero-initialised. With EI_UTF8 the calls that take or give text take their
// UTF-8 versions (game/system_text.h).
const void* ImageRoute(const char* dll, const char* function) {
    for (int i = 0; i < kRoutes; ++i) {
        const Route& r = Routes()[i];
        if (_stricmp(r.dll, dll) != 0 || std::strcmp(r.function, function) != 0) continue;
        g_routed[i] = true;
        return r.replacement;
    }
    return text::SystemRoute(dll, function);
}

// The settings file's reads filtered (paths.cpp: the game's paths in the image's settings file).
void RedirectSettingsReads(void (*filter)(const char* name, unsigned long type, std::string& data)) {
    settings_file::AddReadFilter(filter);
}

} // namespace ei

// Called by the host once ei_hook.dll is loaded, with the game's arguments (what followed "--" on the host's
// command line, as typed). `run` 0: run the start-up actions and route the image's calls, then return (0 when all of
// it is right). `run` 1: then run WinMain.
extern "C" __declspec(dllexport) int __cdecl EiStandaloneMain(int run, const char* arguments) {
    using namespace ei;
    // GetCommandLineA's line: the program, as GetModuleFileNameA(NULL) names it (the host), then the arguments.
    char program[MAX_PATH];
    GetModuleFileNameA(nullptr, program, MAX_PATH);
    const bool hasArguments = arguments && *arguments;
    g_commandLine = std::string("\"") + program + "\"" + (hasArguments ? " " : "") + (hasArguments ? arguments : "");
    g_argumentsAt = std::strlen(program) + 2 + (hasArguments ? 1 : 0);
    SetProcessDPIAware();
    // The missing-code report first: the start-up actions run before WinMain (WideScreen's parses its resolution),
    // so a jump into game.exe's code there is named too, not a silent end.
    AddVectoredExceptionHandler(1, OnOldCode);
    SetUnhandledExceptionFilter(OnUnhandled);
    RunStartupActions();
    // Informational: what the loader put in game.exe's range before the host ran is not ours to move.
    const std::size_t open = CheckGuard();
    const auto* taken = reinterpret_cast<const std::uint32_t*>(GetProcAddress(GetModuleHandleA(nullptr), "EiStandaloneRangeTaken"));
    Log("standalone: game.exe's range %08X-%08X %s (%zu byte(s) open; %u taken before the host's main; the host at %p)",
        static_cast<unsigned>(kImageBegin), static_cast<unsigned>(kImageEnd), open ? "NOT all guarded" : "guarded", open,
        taken ? *taken : 0u, static_cast<void*>(GetModuleHandleA(nullptr)));
    const bool routed = RouteImageCalls();
    Log("standalone: the game's command line%s routed; arguments: '%s'", routed ? "" : " not",
        g_commandLine.c_str() + g_argumentsAt);
    const int missingImports = ImageImportsMissing();
    if (!run) return routed && missingImports == 0 ? 0 : 1;
    if (missingImports != 0) {   // as the system loader: no start without every import (ei_hook.log names them)
        ReportImageFailure("game.exe's imports did not all resolve");
        return 1;
    }
    // As the CRT's start-up: the static initialisers, WinMain, then exit with its result (the CRT's exit runs the
    // functions the initialisers registered with atexit). The multibyte code page (__initmbctable, the C
    // initialiser) is the host CRT's, set at its own start-up; with EI_UTF8 a single-byte one, so the host's
    // multibyte string functions (_splitpath, _mbschr ...) see no lead bytes whatever the system's ANSI code
    // page.
    if constexpr (EI_UTF8 != 0) _setmbcp(_MB_CP_SBCS);
    InstallProfiler();   // EI_PROFILE=1: samples this thread from the start-up on
    render::PrepareDevice();   // the renderer's device made while the game starts up
    static_init::RunStaticInitialisers();
    const int result = app::GameMain(GetModuleHandleA(nullptr), nullptr, g_commandLine.data() + g_argumentsAt,
                                     SW_SHOWNORMAL);
    libcrt::Exit(result);
    return result;
}
