// The standalone image's start-up (docs/notes/standalone.md section 2), called by the exe's main (host.cpp): what
// both runs of the harness use is in src/hook/standalone.cpp. The exe reads no game.exe; its static initialisation has
// entered the game folder and put .rdata, .data and the import slots in its own storage (image_data.cpp,
// image_imports.cpp); here the start-up actions run, then the static initialisers and WinMain (app::GameMain).
#include "ei/hook.h"
#include "hook/log.h"
#include "game/app.h"
#include "standalone/static_init.h"
#include "game/game_data.h"
#include "game/settings_file.h"
#include "lib/crt.h"
#include "render/render.h"

#include <cstdlib>
#include <mbctype.h>
#include <windows.h>

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

std::string g_arguments;   // WinMain's command line: the game's arguments


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
    std::fprintf(stderr, "ei_standalone: %s\n", what);
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

// ---- game.exe's module, as the image's code sees it ------------------------------------------------
// The game's module is the host's: WinMain's hInstance and GetModuleHandleA(NULL) are ei_standalone.exe, which
// has none of game.exe's resources (the strings are literals in our code), so DirectInput and the window class
// take the handle as it is, and GetModuleFileNameA(NULL) names the host. WinMain gets the game's arguments; no
// code of the image asks for GetCommandLineA.

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

// Routes Miles's LoadLibraryA (MilesLoadLibrary). False when mss32.dll is loaded and its import is not found.
bool RouteMilesLoads() {
    HMODULE miles = GetModuleHandleA("mss32.dll");
    if (!miles || RouteImport(reinterpret_cast<std::uintptr_t>(miles), "KERNEL32.dll", "LoadLibraryA",
                              reinterpret_cast<const void*>(&MilesLoadLibrary)))
        return true;
    Log("standalone: mss32.dll's LoadLibraryA not found: EI_Plugin.asi would load");
    return false;
}

} // namespace

namespace {

char g_gameRoot[MAX_PATH];   // EI_GAME_ROOT with a trailing backslash

// The settings file's game paths (Work path, CD-ROM Path) read as EI_GAME_ROOT.
void RedirectedPath(const char* name, unsigned long, std::string& data) {
    if (_stricmp(name, "Work path") == 0 || _stricmp(name, "CD-ROM Path") == 0)
        data.assign(g_gameRoot, std::strlen(g_gameRoot) + 1);
}

// Under ei_launch, which names the data folder in EI_GAME_ROOT (the exe is the build's), the settings file's game
// paths are that folder (game.exe's runs redirect the registry's the same way: src/hook/paths.cpp).
void RedirectSettingsPaths() {
    const DWORD n = GetEnvironmentVariableA("EI_GAME_ROOT", g_gameRoot, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 2) {
        g_gameRoot[0] = 0;
        return;
    }
    if (g_gameRoot[n - 1] != '\\') strcat_s(g_gameRoot, "\\");
    settings_file::AddReadFilter(&RedirectedPath);
    Log("game paths redirected to %s (the settings file's)", g_gameRoot);
}

} // namespace

// The game folder is the working directory: the game's paths (Work path and CD-ROM Path ".\", ".\res\", the saves) and
// game.exe's DLL imports (image_imports.cpp) are found there. ei_launch starts the exe in the data folder it names
// (EI_GAME_ROOT); otherwise the exe is the drop-in one in the game folder, started from anywhere (a shortcut's
// "Start in", a shell in another folder): its own folder. Runs first in the image's static initialisation
// (image_data.cpp's mover).
void EnterGameFolder() {
    if (GetEnvironmentVariableA("EI_GAME_ROOT", nullptr, 0) != 0) return;
    char self[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    if (char* slash = std::strrchr(self, '\\')) *slash = 0;
    if (!SetCurrentDirectoryA(self)) Log("standalone: cannot enter the exe's folder %s (error %lu)", self, GetLastError());
}

// Called by the exe's main with the game's arguments (what followed "--" on its command line, as typed). `run`
// false: run the start-up actions and route the image's calls, then return (0 when all of it is right). `run` true:
// then run WinMain.
int StandaloneMain(bool run, const char* arguments) {
    g_arguments = arguments ? arguments : "";
    Log("ei_standalone started, pid %lu", GetCurrentProcessId());
    RedirectSettingsPaths();
    SetProcessDPIAware();
    // The missing-code report first: the start-up actions run before WinMain (WideScreen's parses its resolution),
    // so a jump into game.exe's code there is named too, not a silent end.
    AddVectoredExceptionHandler(1, OnOldCode);
    SetUnhandledExceptionFilter(OnUnhandled);
    RunStartupActions();
    const bool routed = RouteMilesLoads();
    Log("standalone: Miles's loads%s routed; arguments: '%s'", routed ? "" : " not", g_arguments.c_str());
    const int missingImports = ImageImportsMissing();
    if (!run) return routed && missingImports == 0 ? 0 : 1;
    if (missingImports != 0) {   // as the system loader: no start without every import (ei_hook.log names them)
        ReportImageFailure("game.exe's imports did not all resolve");
        return 1;
    }
    // As the CRT's start-up: the static initialisers, WinMain, then exit with its result (the CRT's exit runs the
    // functions the initialisers registered with atexit). The multibyte code page (__initmbctable, game.exe's C
    // initialiser) is the CRT's, set at its own start-up; here a single-byte one, so _splitpath sees no lead bytes
    // whatever the system's ANSI code page (the text is UTF-8).
    _setmbcp(_MB_CP_SBCS);
    InstallProfiler();   // EI_PROFILE=1: samples this thread from the start-up on
    render::PrepareDevice();   // the renderer's device made while the game starts up
    static_init::RunStaticInitialisers();
    const int result = app::GameMain(GetModuleHandleA(nullptr), nullptr, g_arguments.data(), SW_SHOWNORMAL);
    std::exit(result);
}

} // namespace ei
