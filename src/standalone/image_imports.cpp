// game.exe's imports in ei_hook.dll's storage (S18f; docs/notes/standalone.md, "Own data"). The image's import
// slots are a table of ours: every slot of game.exe's import address table (image_imports.inc, written by
// tools/gen_iat.py from game.exe's import directory: slot, DLL, function or ordinal) is resolved here into the
// storage at the data seam's shift (game_data.h: kImportsBegin-kImportsEnd move with .rdata), so everything that
// calls through a slot by its address (gameapi.h, the static initialisers' Import, Bink's slots, the harness's
// slot routes) reaches our table. The image's command-line import gets its wrapper in place of the export
// (ImageRoute, hook/standalone.cpp); the resource and module imports take the host's module as it is (S18j).
//
// game.exe's own table is guarded with the rest of the old .rdata (S18h, image_data.cpp): the image runs none of
// game.exe's code, so a call through an old slot is a use that missed the seam, reported with the slot's import
// and its code address (OnOldData, DescribeImportSlot). A slot of ours whose import did not resolve leads to a
// trap until the run ends (OnTrap; the launch-free check fails on it).
//
// Runs from image_data.cpp's mover, before every other static constructor of ei_hook.dll: the harness takes
// import slots in its own. In game.exe (the original's runs) nothing happens: the loader filled its table.
#include "game/game_data.h"
#include "hook/log.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace ei {

void DescribeCode(std::uintptr_t address, char* out, std::size_t size);   // hook/standalone.cpp
void ReportImageFailure(const char* what);                                // hook/standalone.cpp
// The wrapper the image takes in place of `dll`!`function`'s export, or nullptr.
const void* ImageRoute(const char* dll, const char* function);   // hook/standalone.cpp

namespace {

constexpr std::uintptr_t kImageBase = 0x00400000;

struct Import {
    std::uintptr_t slot;   // game.exe's slot (an id of the seam)
    const char* dll;
    const char* function;   // nullptr: by ordinal
    unsigned ordinal;
};
constexpr Import kImports[] = {
#include "image_imports.inc"
};

constexpr std::size_t kSlots = (kImportsEnd - kImportsBegin) / 4;
// One INT3 per slot for our table's unresolved slots.
unsigned char* g_traps;
int g_missing = -1;   // imports that did not resolve (-1: not the image)

const Import* ImportAt(std::uintptr_t slot) {
    for (const Import& i : kImports)
        if (i.slot == slot) return &i;
    return nullptr;
}

void Describe(const Import* i, char* out, std::size_t size) {
    if (!i) std::snprintf(out, size, "no import");
    else if (i->function) std::snprintf(out, size, "%s!%s", i->dll, i->function);
    else std::snprintf(out, size, "%s!#%u", i->dll, i->ordinal);
}

// A call that reached a trap: through a slot of ours that did not resolve.
LONG CALLBACK OnTrap(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    const auto at = reinterpret_cast<std::uintptr_t>(r->ExceptionAddress);
    const auto traps = reinterpret_cast<std::uintptr_t>(g_traps);
    if (r->ExceptionCode != EXCEPTION_BREAKPOINT || !g_traps || at - traps >= kSlots) return EXCEPTION_CONTINUE_SEARCH;
    const std::uintptr_t slot = kImportsBegin + 4 * (at - traps);
    char import[160], caller[160], what[480];
    Describe(ImportAt(slot), import, sizeof(import));
    DescribeCode(*reinterpret_cast<const std::uint32_t*>(e->ContextRecord->Esp), caller, sizeof(caller));
    std::snprintf(what, sizeof(what), "a call of game.exe's import %s (slot %08X), which did not resolve, by %s",
                  import, static_cast<unsigned>(slot), caller);
    std::fprintf(stderr, "ei_hook: %s\n", what);
    std::fflush(stderr);
    ReportImageFailure(what);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

bool Exists(const std::string& path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// game.exe's path, as the host was given it (its export EiStandaloneExe); empty when it has none.
std::string ExePath() {
    using ExeFn = const char*(__cdecl*)();
    const auto exe = reinterpret_cast<ExeFn>(GetProcAddress(GetModuleHandleA(nullptr), "EiStandaloneExe"));
    return exe && exe() ? exe() : "";
}

// An import of game.exe: from game.exe's folder (Rebirth's dgVoodoo DDraw.dll), then the working directory
// (mss32.dll, binkw32.dll in the game root), else System32. Rebirth's dinput.dll beside game.exe is a proxy
// that loads addon.dll, which patches game.exe's code: always the system one.
HMODULE LoadImport(const char* name, const std::string& exeFolder) {
    if (_stricmp(name, "dinput.dll") != 0) {
        char cwd[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, cwd);
        for (const std::string& folder : {exeFolder, std::string(cwd)}) {
            if (folder.empty()) continue;
            const std::string path = folder + "\\" + name;
            if (Exists(path)) return LoadLibraryA(path.c_str());
        }
    }
    return LoadLibraryExA(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

void Fail(const char* what) {
    std::fprintf(stderr, "ei_hook: %s\n", what);
    std::fflush(stderr);
    Log("standalone: %s", what);
}

} // namespace

void ResolveImageImports() {
    if (GetModuleHandleA(nullptr) == reinterpret_cast<HMODULE>(kImageBase)) return;   // game.exe
    g_traps = static_cast<unsigned char*>(VirtualAlloc(nullptr, kSlots, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (g_traps) {
        std::memset(g_traps, 0xCC, kSlots);
        DWORD was;
        VirtualProtect(g_traps, kSlots, PAGE_EXECUTE_READ, &was);
        AddVectoredExceptionHandler(1, OnTrap);
    }
    const std::string exe = ExePath();
    const std::size_t slash = exe.find_last_of("\\/");
    const std::string exeFolder = slash == std::string::npos ? std::string() : exe.substr(0, slash);
    int resolved = 0, routed = 0;
    g_missing = 0;
    HMODULE module = nullptr;
    const char* moduleName = nullptr;
    char what[320];
    for (const Import& i : kImports) {
        const std::size_t index = (i.slot - kImportsBegin) / 4;
        auto& ours = GameData<const void*>(i.slot);
        ours = g_traps ? g_traps + index : nullptr;
        if (!moduleName || _stricmp(moduleName, i.dll) != 0) {
            moduleName = i.dll;
            module = LoadImport(i.dll, exeFolder);
            if (!module) {
                std::snprintf(what, sizeof(what), "cannot load game.exe's import %s (error %lu)", i.dll, GetLastError());
                Fail(what);
            }
        }
        const char* symbol = i.function ? i.function : MAKEINTRESOURCEA(i.ordinal);
        const void* real = module ? reinterpret_cast<const void*>(GetProcAddress(module, symbol)) : nullptr;
        if (!real) {
            if (module) {
                char import[160];
                Describe(&i, import, sizeof(import));
                std::snprintf(what, sizeof(what), "game.exe's import %s not found", import);
                Fail(what);
            }
            ++g_missing;
            continue;
        }
        const void* route = i.function ? ImageRoute(i.dll, i.function) : nullptr;
        ours = route ? route : real;
        ++resolved;
        routed += route != nullptr;
    }
    Log("standalone: game.exe's %d import slots in ei_hook.dll's table at %08X: %d resolved (%d routed), %d missing; "
        "game.exe's own table guarded",
        static_cast<int>(sizeof(kImports) / sizeof(kImports[0])), static_cast<unsigned>(GameAddress(kImportsBegin)),
        resolved, routed, g_missing);
}

int ImageImportsMissing() { return g_missing; }

void DescribeImportSlot(std::uintptr_t slot, char* out, std::size_t size) { Describe(ImportAt(slot), out, size); }

} // namespace ei
