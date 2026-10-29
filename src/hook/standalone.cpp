// The harness's two homes (ei::Standalone): linked into ei_standalone.exe (the image; its start-up is
// src/standalone/standalone_main.cpp) or ei_harness.dll inside game.exe (the original's runs). What both use: the
// run's kind, the code addresses as the log names them, and the image's failure reports' handler.
#include "ei/hook.h"
#include "log.h"
#include "game/game_data.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ei {

namespace {

void (*g_missingCodeHandler)(const char* what);

} // namespace

// A code address as the log names it: inside game.exe's code plainly, elsewhere as module+offset.
void DescribeCode(std::uintptr_t address, char* out, std::size_t size) {
    HMODULE module = nullptr;
    char path[MAX_PATH];
    if (!InGameText(address) &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module) &&
        GetModuleFileNameA(module, path, MAX_PATH)) {
        // "<module>+offset", as the harness's crash log has it (tools/resolve_crash.py).
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

// ei_standalone.exe exports EiStandaloneHost; game.exe exports nothing.
bool Standalone() {
    static const bool standalone = GetProcAddress(GetModuleHandleA(nullptr), "EiStandaloneHost") != nullptr;
    return standalone;
}

const char* OwnModuleName() { return Standalone() ? "ei_standalone" : "ei_harness"; }

void SetMissingCodeHandler(void (*handler)(const char* what)) { g_missingCodeHandler = handler; }

void ReportImageFailure(const char* what) {
    Log("standalone: %s", what);
    if (g_missingCodeHandler) g_missingCodeHandler(what);
}

} // namespace ei
