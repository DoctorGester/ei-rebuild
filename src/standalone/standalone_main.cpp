// The standalone image's start-up (docs/notes/standalone.md section 2), called by the exe's main (host.cpp): what
// both runs of the harness use is in src/hook/standalone.cpp. The exe reads no game.exe; its static initialisation has
// entered the game folder and resolved game.exe's imports into a table of its own (image_imports.cpp); here the
// start-up actions run, then the static initialisers and WinMain (app::GameMain).
#include "ei/hook.h"
#include "hook/log.h"
#include "hook/thread_context.h"
#include "game/app.h"
#include "standalone/static_init.h"
#include "game/settings.h"
#include "lib/crt.h"
#include "platform/platform.h"
#include "render/render.h"

#include <cstdlib>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace ei {

int ImageImportsMissing();   // standalone/image_imports.cpp

// A code address as the log names it (standalone.cpp).
void DescribeCode(std::uintptr_t address, char* out, std::size_t size);

// The return address on top of an exception's stack: a call's caller.
std::uintptr_t ReturnAddress(const CONTEXT& context) {
    return *reinterpret_cast<const std::uintptr_t*>(StackPointer(context));
}

// A failure of the image (no code at an address, an import that did not resolve): logged, and handed to the
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
    platform::EndProcess(3);
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
    if (execute) ReportNoCode(at, ReturnAddress(*e->ContextRecord));
    char code[160], what[320];
    DescribeCode(at, code, sizeof(code));
    std::snprintf(what, sizeof(what), "%s of game.exe's code at %08X by %s: the image's code section is guarded",
                  r->ExceptionInformation[0] == 1 ? "write" : "read", static_cast<unsigned>(data), code);
    std::fprintf(stderr, "ei-rebuild: %s\n", what);
    std::fflush(stderr);
    ReportImageFailure(what);
    platform::EndProcess(3);
}

LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* e) {
    Log("standalone: unhandled exception %08lX at %p", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress);
    platform::EndProcess(4);
}

char g_gameRoot[MAX_PATH];   // EI_GAME_ROOT with a trailing backslash

// The settings file's game paths (Work path, CD-ROM Path) read as EI_GAME_ROOT.
void RedirectedPath(const char* name, std::string& value) {
    if (_stricmp(name, "Work path") == 0 || _stricmp(name, "CD-ROM Path") == 0)
        value = g_gameRoot;
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
    settings::AddReadFilter(&RedirectedPath);
    Log("game paths redirected to %s (the settings file's)", g_gameRoot);
}

} // namespace

// The game folder is the working directory: the game's paths (Work path and CD-ROM Path ".\", ".\res\", the saves) and
// game.exe's DLL imports (image_imports.cpp) are found there. ei_launch starts the exe in the data folder it names
// (EI_GAME_ROOT); otherwise the exe is the drop-in one in the game folder, started from anywhere (a shortcut's
// "Start in", a shell in another folder): its own folder. Runs first in the image's static initialisation
// (image_imports.cpp).
void EnterGameFolder() {
    if (!platform::EnvironmentVariable("EI_GAME_ROOT").empty()) return;
    std::string self = platform::ProgramPath();
    if (self.empty()) return;
    self.resize(self.find_last_of('\\') == std::string::npos ? self.size() : self.find_last_of('\\'));
    std::uint32_t error = 0;
    if (!platform::SetCurrentFolder(self, &error))
        Log("standalone: cannot enter the exe's folder %s (error %u)", self.c_str(), error);
}

// Called by the exe's main with the game's arguments (what followed "--" on its command line, as typed). `run`
// false: run the start-up actions and route the image's calls, then return (0 when all of it is right). `run` true:
// then run WinMain.
int StandaloneMain(bool run, const char* arguments) {
    g_arguments = arguments ? arguments : "";
    Log("ei-rebuild started, pid %lu", GetCurrentProcessId());
    RedirectSettingsPaths();
    SetProcessDPIAware();
    // The missing-code report first: the start-up actions run before WinMain, so a jump into game.exe's code there is
    // named too, not a silent end.
    AddVectoredExceptionHandler(1, OnOldCode);
    SetUnhandledExceptionFilter(OnUnhandled);
    RunStartupActions();
    Log("standalone: arguments: '%s'", g_arguments.c_str());
    const int missingImports = ImageImportsMissing();
    if (!run) return missingImports == 0 ? 0 : 1;
    if (missingImports != 0) {   // as the system loader: no start without every import (ei-rebuild.log names them)
        ReportImageFailure("game.exe's imports did not all resolve");
        return 1;
    }
    // As the CRT's start-up: the static initialisers, WinMain, then exit with its result (the CRT's exit runs the
    // functions the initialisers registered with atexit).
    InstallProfiler();   // EI_PROFILE=1: samples this thread from the start-up on
    render::PrepareDevice();   // the renderer's device made while the game starts up
    static_init::RunStaticInitialisers();
    const int result = app::GameMain(platform::ProgramInstance(), nullptr, g_arguments.data(), SW_SHOWNORMAL);
    std::exit(result);
}

} // namespace ei
