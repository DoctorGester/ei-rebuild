// Automated test harness (verify build only).
//
// When ei_launch runs with --harness <scenario>, it sets EI_HARNESS to the scenario path. The
// scenario is a text file of steps, run on the game's main thread from its message loop:
//
//   wait <milliseconds>       let the game run
//   console <command line>    run a console command (the game's own console: exec, server, give,
//                             ...). After the cheat word ("console thingamabob"), "@<command>" runs a
//                             world script command and "#<expression>" prints a script expression.
//   newgame                   start a new single-player game, as the main menu does (no intro)
//   load <slot>               load a saved game (a folder in harness_saves, see below)
//   check <expression>        print the script expression through the console ("#<expression>")
//                             and require a nonzero result (waits up to 3 s for the output)
//   speed <ms>                run one world update per <ms> of real time (game default 55, its own
//                             speed-up key 27; smaller is faster); 0 leaves it to the game
//   waitfor <seconds> <expression>
//                             evaluate the expression every 250 ms until it is nonzero; fail if
//                             that takes longer than <seconds>
//   expect <text>             require <text> in the console output since the previous expect
//   reject <text>             fail if <text> appears in the console output from now on
//   report                    write the verify totals to harness_report.txt
//   quit                      write the report and exit the game through the console
//   # comment
//
// Everything the console prints is captured into the report. The report ends with "RESULT: PASS"
// (no verify mismatches, failed checks or rejected output) or "RESULT: FAIL"; ei_launch --wait turns
// that into its exit code.
//
// Harness runs are hands-off and leave the install alone: no startup movies, the tutorial option
// off, saves and config\tutorial.ini kept in harness_saves next to the DLL, no registry writes.
// A watchdog fails and ends a run that stops making progress (e.g. a message box), and ei_launch
// ends a game that hangs after "quit".
#include "verify.h"

#include "../../src/hook/log.h"
#include "../../src/lib/mfc.h"

#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ei::verify {

namespace {
constexpr std::uintptr_t kPeekMessageASlot = 0x0073B458;   // game.exe import table

// The console's streambuf (class with vtable 00740034, instance 007C5868): overflow() collects the
// text in a CString at +0x50; sync() (slot 1, 005DC1B0) hands the finished line to the console
// window and clears it. Hooking sync gives every line the console prints.
constexpr std::uintptr_t kConsoleStreambufSyncSlot = 0x00740034 + 4;
using SyncFn = int(__thiscall*)(void*);
using OverflowFn = int(__thiscall*)(void*, int);
SyncFn g_sync;

// 005DC620: the console's command executor; the command text is passed in ECX.
__declspec(naked) void __fastcall ConsoleExecute(const char*) EI_ORIGINAL(0x005DC620)

struct Step {
    std::string verb;
    std::string argument;
};

std::vector<Step> g_steps;
std::size_t g_next = 0;
DWORD g_waitUntil = 0;
volatile bool g_quitting = false;
bool g_running = false;
bool g_inStep = false;
char g_reportPath[MAX_PATH];

std::vector<std::string> g_console;          // every console line so far
std::size_t g_expectFrom = 0;                // first line "expect" looks at
std::vector<std::string> g_rejected;         // texts that must not appear
std::vector<std::string> g_failures;
// A pending "check": the console line index its output starts at, and when to give up.
bool g_checking = false;
std::string g_checkExpr;
std::size_t g_checkFrom = 0;
DWORD g_checkDeadline = 0;
// A pending "waitfor": the expression, when it was last evaluated (its output starts at
// g_waitForFrom), when to evaluate it next, and when to give up.
bool g_waitingFor = false;
bool g_waitForPending = false;
std::string g_waitForExpr;
std::size_t g_waitForFrom = 0;
DWORD g_waitForNext = 0;
DWORD g_waitForDeadline = 0;
DWORD g_waitForStart = 0;

int __fastcall ConsoleSync(void* self, void*) {
    OverflowFn overflow = (*reinterpret_cast<OverflowFn**>(self))[7];
    overflow(self, -1);   // move pending characters into the line (the original does this too)
    const char* text = *reinterpret_cast<const char**>(static_cast<char*>(self) + 0x50);
    if (text && *text) {
        std::string line(text);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        Log("console: %s", line.c_str());
        for (const std::string& r : g_rejected)
            if (line.find(r) != std::string::npos) g_failures.push_back("rejected output: " + line);
        g_console.push_back(std::move(line));
    }
    return g_sync(self);
}
decltype(&::PeekMessageA) g_peekMessage;

// 007AFE20: the game's pending state change, carried out by the main loop (0057C700) and then
// reset to 0. +0 kind (2 = load the save in slot <argument>, 3 = new game, 4 = go to zone <argument>, 6 = start
// a multiplayer server on map <argument>), +4 the argument.
struct GameRequest {
    int kind;
    mfc::CString argument;
};
GameRequest& g_request = *reinterpret_cast<GameRequest*>(0x007AFE20);

// Crash diagnostics: logs access violations and other fatal exceptions with the faulting address
// and the code addresses found on the stack (game.exe addresses as is, ours as ei_hook+offset).
// First-chance, so exceptions the game handles itself show up too; only the first few are logged.
std::uintptr_t g_selfBase, g_selfEnd;

void LogCodeAddress(const char* label, std::uintptr_t a) {
    if (a >= 0x00401000 && a < 0x00730000) Log("  %s %08X (game.exe)", label, unsigned(a));
    else if (a >= g_selfBase && a < g_selfEnd) Log("  %s ei_hook+%X", label, unsigned(a - g_selfBase));
}

void LogModuleOf(std::uintptr_t a) {
    HMODULE m = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(a), &m))
        GetModuleFileNameA(m, name, MAX_PATH);
    Log("  in %s+%X (thread %lu)", name, unsigned(a - reinterpret_cast<std::uintptr_t>(m)), GetCurrentThreadId());
}

LONG CALLBACK CrashLogger(EXCEPTION_POINTERS* e) {
    static int logged = 0;
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    if (logged++ >= 5) return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT* c = e->ContextRecord;
    Log("harness: exception %08lX at %08lX (address %08lX); eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX "
        "esi=%08lX edi=%08lX ebp=%08lX esp=%08lX",
        code, c->Eip,
        e->ExceptionRecord->NumberParameters > 1 ? static_cast<unsigned long>(e->ExceptionRecord->ExceptionInformation[1]) : 0ul,
        c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    LogModuleOf(c->Eip);
    LogCodeAddress("at", c->Eip);
    const auto* stack = reinterpret_cast<const std::uintptr_t*>(c->Esp);
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(stack, &mbi, sizeof(mbi));
    const auto* stackEnd = reinterpret_cast<const std::uintptr_t*>(static_cast<char*>(mbi.BaseAddress) + mbi.RegionSize);
    for (int i = 0; i < 256 && stack + i < stackEnd; ++i) LogCodeAddress("stack", stack[i]);
    return EXCEPTION_CONTINUE_SEARCH;
}

template <class T>
void PatchSlot(std::uintptr_t address, T replacement, T& original) {
    auto* slot = reinterpret_cast<T*>(address);
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    original = *slot;
    *slot = replacement;
    VirtualProtect(slot, sizeof(*slot), old, &old);
}

// Saves. The game keeps them under <game root>saves\ and rewrites saves\current\ whenever it
// starts or loads a game, so a harness run would overwrite the player's own progress. In harness
// runs every path whose directory is "saves" goes to harness_saves\ next to the DLL instead.
constexpr std::uintptr_t kCreateDirectoryASlot = 0x0073B1B8;   // game.exe import table
constexpr std::uintptr_t kCopyFileASlot = 0x0073B1D8;
constexpr std::uintptr_t kRemoveDirectoryASlot = 0x0073B1F0;
constexpr std::uintptr_t kDeleteFileASlot = 0x0073B20C;
constexpr std::uintptr_t kFindFirstFileASlot = 0x0073B210;
constexpr std::uintptr_t kCreateFileASlot = 0x0073B218;
constexpr std::uintptr_t kGetFileAttributesASlot = 0x0073B2F8;
constexpr std::uintptr_t kSetFileAttributesASlot = 0x0073B304;
constexpr std::uintptr_t kMoveFileASlot = 0x0073B40C;

char g_savesDir[MAX_PATH];
decltype(&::CreateFileA) g_createFile;
decltype(&::DeleteFileA) g_deleteFile;
decltype(&::CopyFileA) g_copyFile;
decltype(&::MoveFileA) g_moveFile;
decltype(&::FindFirstFileA) g_findFirstFile;
decltype(&::CreateDirectoryA) g_createDirectory;
decltype(&::RemoveDirectoryA) g_removeDirectory;
decltype(&::GetFileAttributesA) g_getFileAttributes;
decltype(&::SetFileAttributesA) g_setFileAttributes;

// Returns `path` with everything up to its "\saves" directory replaced by g_savesDir, or `path`
// itself when it has no such directory. `buffer` holds the rewritten path.
const char* SavesPath(const char* path, char (&buffer)[MAX_PATH]) {
    if (!path) return path;
    // config\tutorial.ini (which tutorial windows were shown) is rewritten on every exit; harness
    // runs keep theirs in harness_saves too.
    std::size_t length = std::strlen(path);
    if (length >= 20 && _stricmp(path + length - 20, "\\config\\tutorial.ini") == 0) {
        if (std::snprintf(buffer, MAX_PATH, "%s\\tutorial.ini", g_savesDir) >= MAX_PATH) return path;
        return buffer;
    }
    for (const char* p = path; (p = std::strchr(p, '\\')) != nullptr; ++p) {
        if (_strnicmp(p, "\\saves", 6) == 0 && (p[6] == '\\' || p[6] == '/' || p[6] == '\0')) {
            if (std::snprintf(buffer, MAX_PATH, "%s%s", g_savesDir, p + 6) >= MAX_PATH) return path;
            return buffer;
        }
    }
    return path;
}

HANDLE WINAPI RedirectedCreateFile(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                   DWORD disposition, DWORD flags, HANDLE templ) {
    char b[MAX_PATH];
    return g_createFile(SavesPath(name, b), access, share, sa, disposition, flags, templ);
}

// Registry: harness runs never write the game's settings (it saves its options on exit).
constexpr std::uintptr_t kRegSetValueExASlot = 0x0073B018;   // game.exe import table
decltype(&::RegSetValueExA) g_regSetValue;

LONG WINAPI SkippedRegSetValue(HKEY, LPCSTR name, DWORD, DWORD, const BYTE*, DWORD) {
    static bool logged = false;
    if (!logged) Log("harness: registry writes are skipped (first: %s)", name ? name : "(default)");
    logged = true;
    return ERROR_SUCCESS;
}

// The "show tutorial" option (options object 0079BC70 + 0x208, registry value ShowTutorial). With it
// off, tutorial windows open only where the game forces them (005F86F0, 005F8A80). The harness
// clears it every frame, since the options are reloaded when a game starts.
int& g_showTutorial = *reinterpret_cast<int*>(0x0079BE78);

// Game speed. The main loop runs one world update per 0078FF34 milliseconds of real time (55 by
// default; the game's own speed-up key, keypad +, sets 27). "speed <ms>" holds it at <ms> every
// frame, since loading a zone resets it. 0 = leave it to the game.
unsigned& g_tickLength = *reinterpret_cast<unsigned*>(0x0078FF34);
unsigned g_heldTickLength = 0;

// Startup movies: config\movie.ini lists them per section ("Start": 1C.bik, Nival.bik), read with
// GetPrivateProfileStringA. Harness runs read the "movies" keys as empty, so nothing plays.
constexpr std::uintptr_t kGetPrivateProfileStringASlot = 0x0073B23C;   // game.exe import table
decltype(&::GetPrivateProfileStringA) g_getProfileString;

DWORD WINAPI HarnessGetProfileString(LPCSTR app, LPCSTR key, LPCSTR fallback, LPSTR out, DWORD size, LPCSTR file) {
    std::size_t length = file ? std::strlen(file) : 0;
    if (key && _stricmp(key, "movies") == 0 && length >= 9 && _stricmp(file + length - 9, "movie.ini") == 0) {
        if (out && size) out[0] = '\0';
        return 0;
    }
    return g_getProfileString(app, key, fallback, out, size, file);
}

BOOL WINAPI RedirectedDeleteFile(LPCSTR name) {
    char b[MAX_PATH];
    return g_deleteFile(SavesPath(name, b));
}
BOOL WINAPI RedirectedCopyFile(LPCSTR from, LPCSTR to, BOOL failIfExists) {
    char b1[MAX_PATH], b2[MAX_PATH];
    return g_copyFile(SavesPath(from, b1), SavesPath(to, b2), failIfExists);
}
BOOL WINAPI RedirectedMoveFile(LPCSTR from, LPCSTR to) {
    char b1[MAX_PATH], b2[MAX_PATH];
    return g_moveFile(SavesPath(from, b1), SavesPath(to, b2));
}
HANDLE WINAPI RedirectedFindFirstFile(LPCSTR name, LPWIN32_FIND_DATAA data) {
    char b[MAX_PATH];
    return g_findFirstFile(SavesPath(name, b), data);
}
BOOL WINAPI RedirectedCreateDirectory(LPCSTR name, LPSECURITY_ATTRIBUTES sa) {
    char b[MAX_PATH];
    return g_createDirectory(SavesPath(name, b), sa);
}
BOOL WINAPI RedirectedRemoveDirectory(LPCSTR name) {
    char b[MAX_PATH];
    return g_removeDirectory(SavesPath(name, b));
}
DWORD WINAPI RedirectedGetFileAttributes(LPCSTR name) {
    char b[MAX_PATH];
    return g_getFileAttributes(SavesPath(name, b));
}
BOOL WINAPI RedirectedSetFileAttributes(LPCSTR name, DWORD attributes) {
    char b[MAX_PATH];
    return g_setFileAttributes(SavesPath(name, b), attributes);
}

void WriteReport() {
    FILE* f = nullptr;
    if (fopen_s(&f, g_reportPath, "w") != 0 || !f) return;
    unsigned long mismatches = 0;
    ForEachCount([&](const char* name, unsigned long calls, unsigned long bad) {
        std::fprintf(f, "%-48s %10lu calls %8lu mismatches\n", name, calls, bad);
        mismatches += bad;
    });
    std::fprintf(f, "\nconsole output:\n");
    for (const std::string& line : g_console) std::fprintf(f, "  %s\n", line.c_str());
    for (const std::string& failure : g_failures) std::fprintf(f, "FAILED: %s\n", failure.c_str());
    std::fprintf(f, "RESULT: %s\n", mismatches || !g_failures.empty() ? "FAIL" : "PASS");
    std::fclose(f);
    Log("harness: report written (%lu mismatches, %u failures)", mismatches, unsigned(g_failures.size()));
}

void RunSteps() {
    g_showTutorial = 0;
    if (g_heldTickLength) g_tickLength = g_heldTickLength;
    if (g_checking) {
        // The script prints its value on a later frame; take the first new line.
        if (g_console.size() > g_checkFrom) {
            const std::string& out = g_console[g_checkFrom];
            char* end = nullptr;
            double value = std::strtod(out.c_str(), &end);
            bool ok = end != out.c_str() && value != 0;
            Log("harness: check %s -> %s (%s)", g_checkExpr.c_str(), out.c_str(), ok ? "ok" : "FAILED");
            if (!ok) g_failures.push_back("check " + g_checkExpr + " printed '" + out + "'");
            g_checking = false;
        } else if (GetTickCount() >= g_checkDeadline) {
            Log("harness: check %s printed nothing", g_checkExpr.c_str());
            g_failures.push_back("check " + g_checkExpr + " printed nothing");
            g_checking = false;
        } else {
            return;
        }
    }
    if (g_waitingFor) {
        DWORD now = GetTickCount();
        if (g_waitForPending && g_console.size() > g_waitForFrom) {
            const std::string& out = g_console[g_waitForFrom];
            char* end = nullptr;
            double value = std::strtod(out.c_str(), &end);
            g_waitForPending = false;
            if (end != out.c_str() && value != 0) {
                Log("harness: waitfor %s done after %lu ms", g_waitForExpr.c_str(), now - g_waitForStart);
                g_waitingFor = false;
            } else {
                g_waitForNext = now + 250;
            }
        }
        if (g_waitingFor && now >= g_waitForDeadline) {
            Log("harness: waitfor %s timed out", g_waitForExpr.c_str());
            g_failures.push_back("waitfor " + g_waitForExpr + " timed out");
            g_waitingFor = false;
        }
        if (g_waitingFor) {
            if (!g_waitForPending && now >= g_waitForNext) {
                g_waitForFrom = g_console.size();
                g_waitForPending = true;
                ConsoleExecute(("#" + g_waitForExpr).c_str());
            }
            return;
        }
    }
    while (g_next < g_steps.size()) {
        if (GetTickCount() < g_waitUntil) return;
        const Step& s = g_steps[g_next++];
        Log("harness: %s %s", s.verb.c_str(), s.argument.c_str());
        if (s.verb == "wait") {
            g_waitUntil = GetTickCount() + std::strtoul(s.argument.c_str(), nullptr, 10);
        } else if (s.verb == "console") {
            ConsoleExecute(s.argument.c_str());
        } else if (s.verb == "newgame") {
            // What the main menu's new-game button does after the intro movie (00632940).
            *reinterpret_cast<unsigned char*>(0x00784750) = 0;
            g_request.kind = 3;
        } else if (s.verb == "load") {
            g_request.argument = s.argument.c_str();
            g_request.kind = 2;
        } else if (s.verb == "check") {
            g_checking = true;
            g_checkExpr = s.argument;
            g_checkFrom = g_console.size();
            g_checkDeadline = GetTickCount() + 3000;
            ConsoleExecute(("#" + s.argument).c_str());
            return;
        } else if (s.verb == "speed") {
            g_heldTickLength = std::strtoul(s.argument.c_str(), nullptr, 10);
        } else if (s.verb == "waitfor") {
            char* rest = nullptr;
            unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            while (rest && *rest == ' ') ++rest;
            g_waitingFor = true;
            g_waitForPending = false;
            g_waitForExpr = rest ? rest : "";
            g_waitForStart = g_waitForNext = GetTickCount();
            g_waitForDeadline = g_waitForStart + seconds * 1000;
            return;
        } else if (s.verb == "expect") {
            bool found = false;
            for (std::size_t i = g_expectFrom; i < g_console.size() && !found; ++i)
                found = g_console[i].find(s.argument) != std::string::npos;
            Log("harness: expect '%s': %s", s.argument.c_str(), found ? "found" : "MISSING");
            if (!found) g_failures.push_back("expected console output '" + s.argument + "'");
            g_expectFrom = g_console.size();
        } else if (s.verb == "reject") {
            g_rejected.push_back(s.argument);
        } else if (s.verb == "report") {
            WriteReport();
        } else if (s.verb == "quit") {
            WriteReport();
            // The game can hang on the way out (Rebirth does). The quit console command calls exit(),
            // which stops our watchdog thread too, so ei_launch ends the process when it sees
            // harness_quit and the game is still running 10 s later.
            char marker[MAX_PATH];
            strcpy_s(marker, g_reportPath);
            if (char* slash = std::strrchr(marker, '\\')) strcpy_s(slash + 1, marker + MAX_PATH - (slash + 1), "harness_quit");
            if (FILE* f = nullptr; fopen_s(&f, marker, "w") == 0 && f) std::fclose(f);
            g_quitting = true;
            ConsoleExecute("quit");
            return;
        } else {
            Log("harness: unknown step '%s'", s.verb.c_str());
        }
    }
}

// Watchdog thread: makes sure every harness run ends without Task Manager. After "quit" the game
// gets 5 s to exit by itself (under Rebirth it hangs on the way out). If the scenario has not
// finished within the sum of its waits plus 3 minutes (for example a modal message box blocks the
// main thread), the report is written with a failure and the process ends.
DWORD g_budget = 180000;

DWORD WINAPI Watchdog(void*) {
    const DWORD start = GetTickCount();
    for (;;) {
        Sleep(250);
        if (g_quitting) {
            Sleep(5000);
            Log("harness: the game did not exit 5 s after quit; terminating it");
            TerminateProcess(GetCurrentProcess(), 0);
        }
        if (GetTickCount() - start >= g_budget) {
            Log("harness: scenario timed out after %lu s (step %u of %u); terminating the game",
                (GetTickCount() - start) / 1000, unsigned(g_next), unsigned(g_steps.size()));
            g_failures.push_back("scenario timed out (the game stopped running steps; a message box?)");
            WriteReport();
            TerminateProcess(GetCurrentProcess(), 1);
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
        if (g_steps.back().verb == "wait") g_budget += std::strtoul(g_steps.back().argument.c_str(), nullptr, 10);
        if (g_steps.back().verb == "waitfor") g_budget += 1000 * std::strtoul(g_steps.back().argument.c_str(), nullptr, 10);
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

        MODULEINFO selfInfo{};
        GetModuleInformation(GetCurrentProcess(), self, &selfInfo, sizeof(selfInfo));
        g_selfBase = reinterpret_cast<std::uintptr_t>(selfInfo.lpBaseOfDll);
        g_selfEnd = g_selfBase + selfInfo.SizeOfImage;
        AddVectoredExceptionHandler(1, &CrashLogger);

        strcpy_s(g_savesDir, g_reportPath);
        if (char* slash = std::strrchr(g_savesDir, '\\')) strcpy_s(slash + 1, g_savesDir + MAX_PATH - (slash + 1), "harness_saves");
        CreateDirectoryA(g_savesDir, nullptr);

        PatchSlot(kPeekMessageASlot, &PeekMessageHook, g_peekMessage);
        PatchSlot(kConsoleStreambufSyncSlot, reinterpret_cast<SyncFn>(&ConsoleSync), g_sync);
        PatchSlot(kCreateFileASlot, &RedirectedCreateFile, g_createFile);
        PatchSlot(kDeleteFileASlot, &RedirectedDeleteFile, g_deleteFile);
        PatchSlot(kCopyFileASlot, &RedirectedCopyFile, g_copyFile);
        PatchSlot(kMoveFileASlot, &RedirectedMoveFile, g_moveFile);
        PatchSlot(kFindFirstFileASlot, &RedirectedFindFirstFile, g_findFirstFile);
        PatchSlot(kCreateDirectoryASlot, &RedirectedCreateDirectory, g_createDirectory);
        PatchSlot(kRemoveDirectoryASlot, &RedirectedRemoveDirectory, g_removeDirectory);
        PatchSlot(kGetFileAttributesASlot, &RedirectedGetFileAttributes, g_getFileAttributes);
        PatchSlot(kSetFileAttributesASlot, &RedirectedSetFileAttributes, g_setFileAttributes);
        PatchSlot(kRegSetValueExASlot, &SkippedRegSetValue, g_regSetValue);
        PatchSlot(kGetPrivateProfileStringASlot, &HarnessGetProfileString, g_getProfileString);
        Log("harness: saves redirected to %s", g_savesDir);
        if (HANDLE watchdog = CreateThread(nullptr, 0, &Watchdog, nullptr, 0, nullptr)) CloseHandle(watchdog);
        g_running = true;
        Log("harness: %u steps from %s", unsigned(g_steps.size()), path);
    }
} g_harness;
} // namespace

} // namespace ei::verify
