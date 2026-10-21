#include "ei/hook.h"
#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <share.h>

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

// The file stays open for the whole run and each line is flushed as it is written (a crash loses
// nothing). Opening and closing it per line made the antivirus scan it on every close: about 10,000
// scans per harness run, most of its scanning during the suite.
static FILE* LogFile() {
    static FILE* f = [] {
        if (!g_logPath[0]) InitLogPath();
        return _fsopen(g_logPath, "a", _SH_DENYNO);
    }();
    return f;
}

static void WriteStamp(FILE* f) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
}

void Log(const char* fmt, ...) {
    FILE* f = LogFile();
    if (!f) return;
    _lock_file(f);
    WriteStamp(f);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fflush(f);
    _unlock_file(f);
}

void LogLines(const char* lines) {
    FILE* f = LogFile();
    if (!f || !*lines) return;
    _lock_file(f);
    for (const char* at = lines; *at;) {
        const char* end = std::strchr(at, '\n');
        if (!end) end = at + std::strlen(at);
        WriteStamp(f);
        std::fwrite(at, 1, static_cast<std::size_t>(end - at), f);
        std::fputc('\n', f);
        at = *end ? end + 1 : end;
    }
    std::fflush(f);
    _unlock_file(f);
}

} // namespace ei

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        ei::Log("ei_hook loaded into pid %lu", GetCurrentProcessId());
        ei::RedirectGamePaths();
        // In ei_standalone.exe the host calls EiStandaloneMain (standalone.cpp) instead.
        if (!ei::Standalone()) ei::InstallHooksAtWinMain();
    }
    return TRUE;
}
