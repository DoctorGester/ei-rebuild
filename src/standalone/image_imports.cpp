// game.exe's imports resolved into the image's own table, before every other static constructor
// (init_seg(compiler)). See docs/notes/standalone.md, "The image's source files".
#include "ei/hook.h"
#include "game/system_text.h"
#include "hook/log.h"
#include "lib/import_table.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#pragma warning(disable : 4074)   // initialisers put in the compiler's initialisation area: the point
#pragma init_seg(compiler)

namespace ei {

void DescribeCode(std::uintptr_t address, char* out, std::size_t size);
void ReportImageFailure(const char* what);
void EnterGameFolder();   // standalone_main.cpp

namespace {

struct Import {
    std::size_t offset;   // its member's in ImportTable (lib/import_table.h)
    const char* dll;
    const char* function;   // nullptr: by ordinal
    unsigned ordinal;
    bool reached;   // a call of our code or the harness goes through the slot (gen_iat.py's reached()): resolved
};
constexpr Import kImports[] = {
#include "image_imports.inc"
};

ImportTable g_table;

constexpr std::size_t kSlots = sizeof(ImportTable) / 4;
// One INT3 per slot for our table's unresolved slots.
unsigned char* g_traps;
int g_missing;   // imports that did not resolve

const Import* ImportAt(std::size_t offset) {
    for (const Import& i : kImports)
        if (i.offset == offset) return &i;
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
    char import[160], caller[160], what[480];
    const Import* i = ImportAt(4 * (at - traps));
    Describe(i, import, sizeof(import));
    DescribeCode(*reinterpret_cast<const std::uint32_t*>(e->ContextRecord->Esp), caller, sizeof(caller));
    std::snprintf(what, sizeof(what), "a call of game.exe's import %s, which %s, by %s", import,
                  i && !i->reached ? "no code was found to call (rerun python tools/gen_iat.py)" : "did not resolve",
                  caller);
    std::fprintf(stderr, "ei-rebuild: %s\n", what);
    std::fflush(stderr);
    ReportImageFailure(what);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

bool Exists(const std::string& path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// An import of game.exe: from the working directory, the game folder (mss32.dll, binkw32.dll in its root), else
// System32. game.exe's own folder is not looked in (the image needs no game.exe).
HMODULE LoadImport(const char* name) {
    char cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, cwd);
    const std::string path = std::string(cwd) + "\\" + name;
    if (Exists(path)) return LoadLibraryA(path.c_str());
    return LoadLibraryExA(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

void Fail(const char* what) {
    std::fprintf(stderr, "ei-rebuild: %s\n", what);
    std::fflush(stderr);
    Log("standalone: %s", what);
}

// Fills the table: each reached slot with its export (or the image's route of it), every other one with a trap.
void ResolveImports() {
    g_traps = static_cast<unsigned char*>(VirtualAlloc(nullptr, kSlots, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (g_traps) {
        std::memset(g_traps, 0xCC, kSlots);
        DWORD was;
        VirtualProtect(g_traps, kSlots, PAGE_EXECUTE_READ, &was);
        AddVectoredExceptionHandler(1, OnTrap);
    }
    int resolved = 0, routed = 0, traps = 0, loaded = 0;
    HMODULE module = nullptr;
    const char* moduleName = nullptr;
    char what[320];
    for (const Import& i : kImports) {
        const std::size_t index = i.offset / 4;
        auto& ours = reinterpret_cast<const void**>(&g_table)[index];
        ours = g_traps ? g_traps + index : nullptr;
        if (!i.reached) {   // a DLL none of whose slots is reached does not load
            ++traps;
            continue;
        }
        if (!moduleName || _stricmp(moduleName, i.dll) != 0) {
            moduleName = i.dll;
            module = LoadImport(i.dll);
            loaded += module != nullptr;
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
        const void* route = i.function ? text::SystemRoute(i.function) : nullptr;
        ours = route ? route : real;
        ++resolved;
        routed += route != nullptr;
    }
    Log("standalone: game.exe's %d imports in the image's table: %d resolved (%d routed), %d missing, %d traps (no "
        "call reaches them); %d DLLs loaded",
        static_cast<int>(sizeof(kImports) / sizeof(kImports[0])), resolved, routed, g_missing, traps, loaded);
}

struct ImportResolver {
    ImportResolver() {
        EnterGameFolder();   // the imports' DLLs are the game folder's
        ResolveImports();
    }
} g_importResolver;

} // namespace

ImportTable& Imports() { return g_table; }

int ImageImportsMissing() { return g_missing; }

} // namespace ei
