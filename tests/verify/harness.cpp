// Automated test harness (verify build only).
//
// When ei_launch runs with --harness <scenario>, it sets EI_HARNESS to the scenario path. The
// scenario is a text file of steps, run on the game's main thread from its message loop:
//
//   wait <milliseconds>       let the game run
//   console <command line>    run a console command (the game's own console: exec, server, give,
//                             quickload, script expressions, ...)
//   report                    write the verify totals to harness_report.txt
//   quit                      write the report and exit the game through the console
//   # comment
//
// The report ends with "RESULT: PASS" (no verify mismatches) or "RESULT: FAIL"; ei_launch --wait
// turns that into its exit code.
#include "verify.h"

#include "../../src/hook/log.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ei::verify {

namespace {
constexpr std::uintptr_t kPeekMessageASlot = 0x0073B458;   // game.exe import table

// 005DC620: the console's command executor; the command text is passed in ECX.
__declspec(naked) void __fastcall ConsoleExecute(const char*) EI_ORIGINAL(0x005DC620)

struct Step {
    std::string verb;
    std::string argument;
};

std::vector<Step> g_steps;
std::size_t g_next = 0;
DWORD g_waitUntil = 0;
DWORD g_exitDeadline = 0;   // after "quit": exit the process if the console did not
bool g_running = false;
bool g_inStep = false;
char g_reportPath[MAX_PATH];
decltype(&::PeekMessageA) g_peekMessage;

void WriteReport() {
    FILE* f = nullptr;
    if (fopen_s(&f, g_reportPath, "w") != 0 || !f) return;
    unsigned long mismatches = 0;
    ForEachCount([&](const char* name, unsigned long calls, unsigned long bad) {
        std::fprintf(f, "%-48s %10lu calls %8lu mismatches\n", name, calls, bad);
        mismatches += bad;
    });
    std::fprintf(f, "RESULT: %s\n", mismatches ? "FAIL" : "PASS");
    std::fclose(f);
    Log("harness: report written (%lu mismatches)", mismatches);
}

void RunSteps() {
    if (g_exitDeadline && GetTickCount() >= g_exitDeadline) {
        Log("harness: console quit did not exit; exiting the process");
        ExitProcess(0);
    }
    while (g_next < g_steps.size()) {
        if (GetTickCount() < g_waitUntil) return;
        const Step& s = g_steps[g_next++];
        Log("harness: %s %s", s.verb.c_str(), s.argument.c_str());
        if (s.verb == "wait") {
            g_waitUntil = GetTickCount() + std::strtoul(s.argument.c_str(), nullptr, 10);
        } else if (s.verb == "console") {
            ConsoleExecute(s.argument.c_str());
        } else if (s.verb == "report") {
            WriteReport();
        } else if (s.verb == "quit") {
            WriteReport();
            ConsoleExecute("quit");
            g_exitDeadline = GetTickCount() + 5000;
            return;
        } else {
            Log("harness: unknown step '%s'", s.verb.c_str());
        }
    }
}

BOOL WINAPI PeekMessageHook(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT remove) {
    if (g_running && !g_inStep) {
        g_inStep = true;   // console commands can pump messages themselves
        RunSteps();
        g_inStep = false;
    }
    return g_peekMessage(msg, hwnd, min, max, remove);
}

bool LoadScenario(const char* path) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) return false;
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        std::string text(line);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
        std::size_t start = text.find_first_not_of(" \t");
        if (start == std::string::npos || text[start] == '#') continue;
        text = text.substr(start);
        std::size_t space = text.find(' ');
        g_steps.push_back({text.substr(0, space), space == std::string::npos ? "" : text.substr(space + 1)});
    }
    std::fclose(f);
    return true;
}

struct Harness {
    Harness() {
        char path[MAX_PATH];
        DWORD n = GetEnvironmentVariableA("EI_HARNESS", path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return;
        if (!LoadScenario(path)) {
            Log("harness: cannot read scenario %s", path);
            return;
        }
        // Report next to ei_hook.log.
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&LoadScenario), &self);
        GetModuleFileNameA(self, g_reportPath, MAX_PATH);
        if (char* slash = std::strrchr(g_reportPath, '\\')) strcpy_s(slash + 1, g_reportPath + MAX_PATH - (slash + 1), "harness_report.txt");
        std::remove(g_reportPath);

        auto* slot = reinterpret_cast<decltype(&::PeekMessageA)*>(kPeekMessageASlot);
        DWORD old;
        VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
        g_peekMessage = *slot;
        *slot = &PeekMessageHook;
        VirtualProtect(slot, sizeof(*slot), old, &old);
        g_running = true;
        Log("harness: %u steps from %s", unsigned(g_steps.size()), path);
    }
} g_harness;
} // namespace

} // namespace ei::verify
