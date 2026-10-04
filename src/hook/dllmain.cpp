#include "ei/hook.h"
#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace ei {

static char g_logPath[MAX_PATH];

void OutputPath(const char* name, char* out) {
    DWORD n = GetEnvironmentVariableA("EI_OUT_DIR", out, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&OutputPath), &self);
        GetModuleFileNameA(self, out, MAX_PATH);
        if (char* slash = strrchr(out, '\\')) *slash = 0;
    }
    CreateDirectoryA(out, nullptr);
    std::size_t used = strlen(out);
    std::snprintf(out + used, MAX_PATH - used, "\\%s", name);
}

// ei_hook.log in the output folder. Set on first use: static constructors (the harness) log before
// DllMain runs.
static void InitLogPath() { OutputPath("ei_hook.log", g_logPath); }

void Log(const char* fmt, ...) {
    if (!g_logPath[0]) InitLogPath();
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
        ei::Log("ei_hook loaded into pid %lu", GetCurrentProcessId());
        ei::RedirectGamePaths();
        ei::InstallHooksAtWinMain();
    }
    return TRUE;
}
