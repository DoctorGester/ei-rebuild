// ei_hook.dll inside ei_standalone.exe (src/standalone/host.cpp; docs/notes/standalone.md section 2).
// The host has laid out game.exe's headers and data at 0x400000 with INT3 in place of its code and
// resolved its imports; here the jumps to our code are written and checked, and WinMain runs.
#include "ei/hook.h"
#include "log.h"
#include "standalone/static_init.h"
#include "game/originals_library.h"
#include "lib/crt.h"
#include "render/render.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ei {

const std::vector<std::pair<std::uintptr_t, void*>>& InstalledHooks();   // hook.cpp
std::uintptr_t FunctionContaining(std::uintptr_t address);              // hook.cpp

namespace {
// game.exe's code section (.text, 0x401000-0x73A670).
constexpr std::uintptr_t kTextBegin = 0x00401000;
constexpr std::uintptr_t kTextEnd = 0x0073A670;
} // namespace

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

// A failure of the image (no code at an address, a use of moved data): logged, and handed to the
// harness's handler (its report) before the process ends.
void ReportImageFailure(const char* what);

namespace {

char g_exePath[MAX_PATH];
std::string g_commandLine;      // as the CRT would see game.exe's: "<game.exe>" and the game's arguments
std::size_t g_argumentsAt = 0;  // where the arguments start in it (WinMain's command line)
HMODULE g_resources;            // game.exe loaded as an image resource
void (*g_missingCodeHandler)(const char* what);

constexpr std::uintptr_t kWinMain = 0x00456440;

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
    ReportImageFailure(what);
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

// ---- game.exe's module, as the image's code sees it ------------------------------------------------
// 0x400000 is not a loaded module in the host's process, yet the game hands it (its hInstance) to
// LoadIconA, LoadStringA, FindResourceA ... The image's import slots of those calls lead here: the
// image's handle becomes game.exe loaded as an image resource, and to the game the process's own
// module (NULL, GetModuleHandleA(NULL)) is game.exe, as in a hooked run. For LoadIconA, LoadCursorA
// and LoadBitmapA NULL names the system's images and stays.

HMODULE ImageModule() { return reinterpret_cast<HMODULE>(kImageBase); }

HMODULE Resources(HMODULE module, bool nullIsExe) {
    return module == ImageModule() || (nullIsExe && !module) ? g_resources : module;
}

HICON WINAPI ImageLoadIcon(HINSTANCE instance, LPCSTR name) { return LoadIconA(Resources(instance, false), name); }
HCURSOR WINAPI ImageLoadCursor(HINSTANCE instance, LPCSTR name) { return LoadCursorA(Resources(instance, false), name); }
HBITMAP WINAPI ImageLoadBitmap(HINSTANCE instance, LPCSTR name) { return LoadBitmapA(Resources(instance, false), name); }
int WINAPI ImageLoadString(HINSTANCE instance, UINT id, LPSTR buffer, int size) {
    return LoadStringA(Resources(instance, true), id, buffer, size);
}
HRSRC WINAPI ImageFindResource(HMODULE module, LPCSTR name, LPCSTR type) {
    return FindResourceA(Resources(module, true), name, type);
}
HGLOBAL WINAPI ImageLoadResource(HMODULE module, HRSRC resource) { return LoadResource(Resources(module, true), resource); }
HMODULE WINAPI ImageModuleHandle(LPCSTR name) { return name ? GetModuleHandleA(name) : ImageModule(); }
// As GetModuleFileNameA: truncated to `size` with its terminator, then ERROR_INSUFFICIENT_BUFFER.
DWORD WINAPI ImageModuleFileName(HMODULE module, LPSTR out, DWORD size) {
    if (module && module != ImageModule()) return GetModuleFileNameA(module, out, size);
    if (size == 0) return 0;
    const DWORD length = static_cast<DWORD>(std::strlen(g_exePath));
    const DWORD copied = length < size ? length : size - 1;
    std::memcpy(out, g_exePath, copied);
    out[copied] = 0;
    if (copied < length) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return size;
    }
    return length;
}
LPSTR WINAPI ImageCommandLine() { return g_commandLine.data(); }
// DirectInput checks its instance is a loaded module (E_INVALIDARG otherwise): the image's handle
// becomes the host's.
using DirectInputCreateExFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
DirectInputCreateExFn g_directInputCreateEx;
HRESULT WINAPI ImageDirectInputCreateEx(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out, LPUNKNOWN outer) {
    if (instance == ImageModule()) instance = GetModuleHandleA(nullptr);
    return g_directInputCreateEx(instance, version, iid, out, outer);
}

// Points `base`'s import slots of `dll`!`function` at `replacement`. Slots are matched by what they hold
// (the export the loader, or the host, resolved), so a module without an import name table works too.
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
// game.exe's code, which here is the image with our jumps: it stays out (its features are Rebirth
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

// Routes the image's resource, module, command-line and DirectInput calls, and Miles's LoadLibraryA. False when
// game.exe's resources cannot be loaded or a call is not found.
bool RouteImageCalls() {
    g_resources = LoadLibraryExA(g_exePath, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!g_resources) {
        Log("standalone: cannot load %s's resources (error %lu)", g_exePath, GetLastError());
        return false;
    }
    struct Route {
        const char* dll;
        const char* function;
        const void* replacement;
    };
    const Route routes[] = {
        {"USER32.dll", "LoadIconA", reinterpret_cast<const void*>(&ImageLoadIcon)},
        {"USER32.dll", "LoadCursorA", reinterpret_cast<const void*>(&ImageLoadCursor)},
        {"USER32.dll", "LoadBitmapA", reinterpret_cast<const void*>(&ImageLoadBitmap)},
        {"USER32.dll", "LoadStringA", reinterpret_cast<const void*>(&ImageLoadString)},
        {"KERNEL32.dll", "FindResourceA", reinterpret_cast<const void*>(&ImageFindResource)},
        {"KERNEL32.dll", "LoadResource", reinterpret_cast<const void*>(&ImageLoadResource)},
        {"KERNEL32.dll", "GetModuleHandleA", reinterpret_cast<const void*>(&ImageModuleHandle)},
        {"KERNEL32.dll", "GetModuleFileNameA", reinterpret_cast<const void*>(&ImageModuleFileName)},
        {"KERNEL32.dll", "GetCommandLineA", reinterpret_cast<const void*>(&ImageCommandLine)},
        {"DINPUT.dll", "DirectInputCreateEx", reinterpret_cast<const void*>(&ImageDirectInputCreateEx)},
    };
    if (HMODULE input = GetModuleHandleA("DINPUT.dll"))
        g_directInputCreateEx = reinterpret_cast<DirectInputCreateExFn>(GetProcAddress(input, "DirectInputCreateEx"));
    bool ok = true;
    for (const Route& r : routes) {
        if (RouteImport(kImageBase, r.dll, r.function, r.replacement)) continue;
        Log("standalone: game.exe's import %s!%s not found to route", r.dll, r.function);
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

bool Standalone() {
    static const bool standalone = GetModuleHandleA(nullptr) != reinterpret_cast<HMODULE>(kImageBase);
    return standalone;
}

const char* StandaloneExePath() { return g_exePath; }

void SetMissingCodeHandler(void (*handler)(const char* what)) { g_missingCodeHandler = handler; }

void ReportImageFailure(const char* what) {
    Log("standalone: %s", what);
    if (g_missingCodeHandler) g_missingCodeHandler(what);
}

} // namespace ei

// Called by the host once the image is laid out, with the game's arguments (what followed "--" on the
// host's command line, as typed). `run` 0: install and check the jumps and route the image's calls,
// then return (0 when all of it is right and WinMain has a jump). `run` 1: then run WinMain.
extern "C" __declspec(dllexport) int __cdecl EiStandaloneMain(const char* exePath, int run, const char* arguments) {
    using namespace ei;
    strcpy_s(g_exePath, exePath);
    const bool hasArguments = arguments && *arguments;
    g_commandLine = std::string("\"") + exePath + "\"" + (hasArguments ? " " : "") + (hasArguments ? arguments : "");
    g_argumentsAt = std::strlen(exePath) + 2 + (hasArguments ? 1 : 0);
    SetProcessDPIAware();
    // The missing-code report first: InstallHooks runs the startup actions (WideScreen's parses its
    // resolution there, S16b), so an INT3 hit before WinMain is named too, not a silent end (S16c).
    AddVectoredExceptionHandler(1, OnBreakpoint);
    SetUnhandledExceptionFilter(OnUnhandled);
    const int installed = InstallHooks();
    const int bad = CheckJumps();
    const bool winMain = *reinterpret_cast<const unsigned char*>(kWinMain) == 0xE9;
    Log("standalone: %d jump(s) written into the image, %d wrong, WinMain %s", installed, bad,
        winMain ? "rewritten" : "missing");
    const bool routed = RouteImageCalls();
    Log("standalone: game.exe's resources, module and command line%s routed; arguments: '%s'", routed ? "" : " not all",
        g_commandLine.c_str() + g_argumentsAt);
    // What the static initialisers call that has no code yet (logged; not a failure of the check).
    static_init::ReportMissingTargets();
    if (!run) return installed > 0 && bad == 0 && winMain && routed ? 0 : 1;
    // As the CRT's start-up: the multibyte code page (__initmbctable, the C initialiser 00783BC0), the
    // static initialisers, WinMain, then exit with its result (the CRT's exit runs the functions the
    // initialisers registered with atexit).
    InstallProfiler();   // EI_PROFILE=1: samples this thread from the start-up on
    render::PrepareDevice();   // the renderer's device made while the game starts up
    libcrt::InitMultibyteTable();
    static_init::RunStaticInitialisers();
    using WinMainFn = int(__stdcall*)(HINSTANCE, HINSTANCE, char*, int);
    const int result = reinterpret_cast<WinMainFn>(kWinMain)(reinterpret_cast<HINSTANCE>(kImageBase), nullptr,
                                                              g_commandLine.data() + g_argumentsAt, SW_SHOWNORMAL);
    libcrt::Exit(result);
    return result;
}
