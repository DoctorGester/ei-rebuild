// game.exe's imports resolved into the program's own table (the slots gameapi.h calls through and the harness
// redirects), before every other static constructor (init_seg(compiler)). From System32: no import is a game
// folder's DLL.
#include "platform/win32/platform_win32.h"
#include "hook/log.h"
#include "platform/win32/import_table.h"

#include <windows.h>

#include <cstddef>
#include <iterator>

#pragma warning(disable : 4074)   // initialisers put in the compiler's initialisation area: the point
#pragma init_seg(compiler)

namespace ei {

namespace {

struct Import {
    std::size_t offset;   // its member's in ImportTable (platform/win32/import_table.h)
    const char* dll;
    const char* function;   // nullptr: by ordinal
    unsigned ordinal;
};
constexpr Import kImports[] = {
#include "image_imports.inc"
};

ImportTable g_table;

// Fills the table: each slot with its export, or the UTF-8 version of it (text::SystemRoute). One that does not
// resolve is logged and stays null.
void ResolveImports() {
    int routed = 0, missing = 0;
    HMODULE module = nullptr;
    const char* moduleName = nullptr;
    for (const Import& i : kImports) {
        if (!moduleName || _stricmp(moduleName, i.dll) != 0) {
            moduleName = i.dll;
            module = LoadLibraryExA(i.dll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        }
        const char* symbol = i.function ? i.function : MAKEINTRESOURCEA(i.ordinal);
        const void* real = module ? reinterpret_cast<const void*>(GetProcAddress(module, symbol)) : nullptr;
        if (!real) {
            Log("game.exe's import %s!%s (#%u) did not resolve (error %lu)", i.dll, i.function ? i.function : "",
                i.ordinal, GetLastError());
            ++missing;
            continue;
        }
        const void* route = i.function ? text::SystemRoute(i.function) : nullptr;
        reinterpret_cast<const void**>(&g_table)[i.offset / sizeof(void*)] = route ? route : real;
        routed += route != nullptr;
    }
    Log("game.exe's %d imports in the program's table: %d routed, %d missing",
        static_cast<int>(std::size(kImports)), routed, missing);
}

struct ImportResolver {
    ImportResolver() { ResolveImports(); }
} g_importResolver;

} // namespace

ImportTable& Imports() { return g_table; }

} // namespace ei
