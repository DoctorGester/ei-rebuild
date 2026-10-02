#include "ei/hook.h"
#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace ei {

static char g_logPath[MAX_PATH];

void Log(const char* fmt, ...) {
    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "a") != 0 || !f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

} // namespace ei

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        GetModuleFileNameA(module, ei::g_logPath, MAX_PATH);
        if (char* dot = strrchr(ei::g_logPath, '.')) strcpy_s(dot, 5, ".log");
        ei::Log("ei_hook loaded into pid %lu", GetCurrentProcessId());
        ei::RedirectGamePaths();
        ei::InstallHooksAtWinMain();
    }
    return TRUE;
}
