// ei_launch: starts the original game.exe the same way the Rebirth starter does (exe from
// starter\Engine, working directory = game root), or <game>\game.exe for a plain install, and loads
// ei_hook.dll into it before the game's own code runs. Nothing in the game folder is modified.
//
// usage: ei_launch [--game <dir>] [--exe <game.exe outside the game dir>] [--dll <ei_hook.dll>]
//                  [--harness <scenario.txt>] [--wait] [--no-hooks] [--rebirth <spec>] [--out <dir>]
//                  [--standalone] [game args...]
// --out puts what the run writes (ei_hook.log, the harness report and saves, ...) in <dir> instead
// of next to ei_hook.dll (EI_OUT_DIR), so several runs can go at once.
// --no-hooks loads ei_hook.dll (so the harness still runs) but installs none of the rewritten
// functions: a baseline run of the original code.
// --rebirth turns Rebirth patch flags on in the rewritten code (EI_REBIRTH; include/ei/rebirth.h):
// "all", or patch names separated by commas, each optionally =0/=1.
// --standalone runs ei_standalone.exe (next to ei_launch.exe; src/standalone/host.cpp) instead of game.exe:
// it lays out game.exe's data without its code and loads ei_hook.dll itself (--run), with the same
// environment, working directory, output folder, job object and time limits as a hooked run. Its own
// output (imports, the INT3 hit) comes through ei_launch's. Game args go after "--" (WinMain gets them as
// game.exe would).
//
// A run with --harness ends whatever state the game is in: the harness's watchdog thread ends it at the
// scenario's "timeout" (60 s unless the scenario says otherwise); if that thread cannot run, ei_launch ends
// it kTimeoutMargin later. The game runs in a job object that is killed with ei_launch, so it (and any
// process it started) never outlives ei_launch. Exit codes of a run: 0 passed, 1 failed, 2 no report /
// not started, 3 and 4 below.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static std::wstring Dir(const std::wstring& path) {
    return path.substr(0, path.find_last_of(L"\\/"));
}

// The windows a game still has open when it is terminated (a dialog it waits on says why), one line
// each: class, title and the text of its child controls.
static void PrintOpenWindows(DWORD pid) {
    EnumWindows([](HWND w, LPARAM p) -> BOOL {
        DWORD owner = 0;
        GetWindowThreadProcessId(w, &owner);
        if (owner != static_cast<DWORD>(p) || !IsWindowVisible(w)) return TRUE;
        wchar_t cls[64] = L"", title[256] = L"";
        GetClassNameW(w, cls, 64);
        GetWindowTextW(w, title, 256);
        std::wstring text;
        EnumChildWindows(w, [](HWND c, LPARAM t) -> BOOL {
            wchar_t buf[512] = L"";
            if (GetWindowTextW(c, buf, 512) > 0) {
                auto& out = *reinterpret_cast<std::wstring*>(t);
                if (!out.empty()) out += L" | ";
                out += buf;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&text));
        for (wchar_t& ch : text)
            if (ch == L'\r' || ch == L'\n') ch = L' ';
        std::wprintf(L"ei_launch: open window [%s] '%s' %s\n", cls, title, text.c_str());
        return TRUE;
    }, static_cast<LPARAM>(pid));
}

// ei_launch's exit codes besides 0 (passed), 1 (failed) and 2 (no report): the run took longer than its limit
// plus the margin and the game had to be ended from outside (run_all.py reports it as TIMEOUT); the game
// could not be ended at all (a thread stuck in a driver call keeps a terminated process alive).
constexpr int kExitTimeout = 4;
constexpr int kExitUnkillable = 5;

// How long past the scenario's own time limit ei_launch waits for the game's watchdog before ending the game.
constexpr ULONGLONG kTimeoutMarginMs = 20000;
constexpr ULONGLONG kDefaultTimeLimitMs = 60000;

// The scenario's "timeout <seconds>" line (the last one counts, as in the harness), else the default.
static ULONGLONG ScenarioTimeLimitMs(const wchar_t* scenario) {
    ULONGLONG limit = kDefaultTimeLimitMs;
    FILE* f = nullptr;
    if (_wfopen_s(&f, scenario, L"r") != 0 || !f) return limit;
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        const char* p = line + std::strspn(line, " \t");
        if (std::strncmp(p, "timeout", 7) == 0 && (p[7] == ' ' || p[7] == '\t'))
            limit = 1000ull * std::strtoul(p + 8, nullptr, 10);
    }
    std::fclose(f);
    return limit;
}

// PrintOpenWindows on a thread of its own: it must not be able to hold ei_launch back, whatever the game does.
static void PrintOpenWindowsBounded(DWORD pid) {
    HANDLE t = CreateThread(nullptr, 0, [](void* p) -> DWORD {
        PrintOpenWindows(static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(p)));
        return 0;
    }, reinterpret_cast<void*>(static_cast<ULONG_PTR>(pid)), 0, nullptr);
    if (!t) return;
    WaitForSingleObject(t, 2000);
    CloseHandle(t);
}

// Ends the game and everything it started. A process whose thread is stuck inside a driver (kernel
// APCs off) stays alive after TerminateProcess; it is tried again for 15 s and then given up on.
// True when the process is gone.
static bool EndGame(HANDLE process, HANDLE job, DWORD pid, UINT code) {
    PrintOpenWindowsBounded(pid);
    for (int attempt = 0; attempt < 3; ++attempt) {
        TerminateProcess(process, code);
        if (job) TerminateJobObject(job, code);
        if (WaitForSingleObject(process, 5000) == WAIT_OBJECT_0) return true;
    }
    std::fwprintf(stderr, L"ei_launch: the game (pid %lu) is still alive 15 s after TerminateProcess; "
                          L"a thread of it is stuck in the kernel (a driver call) and Windows cannot end it yet\n", pid);
    return false;
}

static int Fail(const wchar_t* what) {
    std::fwprintf(stderr, L"ei_launch: %s failed (error %lu)\n", what, GetLastError());
    return 1;
}

// Loads the DLL into the suspended game with a remote LoadLibraryW call. ei_launch is 32-bit like the game,
// so kernel32's address is the same in both processes. 0, or ei_launch's exit code after ending the game.
static int InjectDll(HANDLE process, const std::wstring& dll) {
    std::size_t bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(process, remote, dll.c_str(), bytes, nullptr)) {
        TerminateProcess(process, 1);
        return Fail(L"writing DLL path");
    }
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (!thread) {
        TerminateProcess(process, 1);
        return Fail(L"CreateRemoteThread");
    }
    if (WaitForSingleObject(thread, 60000) != WAIT_OBJECT_0) {
        // The DLL's start-up hangs inside the game (the game is still suspended): end it, do not wait for ever.
        std::fwprintf(stderr, L"ei_launch: loading %s did not finish in 60 s; terminating the game\n", dll.c_str());
        TerminateProcess(process, 3);
        return kExitTimeout;
    }
    DWORD module = 0;
    GetExitCodeThread(thread, &module);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    if (!module) {
        TerminateProcess(process, 1);
        std::fwprintf(stderr, L"ei_launch: LoadLibrary of %s failed inside the game\n", dll.c_str());
        return 1;
    }
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);

    std::wstring gameDir = L"F:\\games\\Evil Islands (Rebirth)";
    std::wstring dll = Dir(self) + L"\\ei_hook.dll";
    std::wstring exeOverride;
    std::wstring harness;
    std::wstring outDir;
    bool wait = false;
    bool standalone = false;
    std::wstring gameArgs;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--game" && i + 1 < argc) gameDir = argv[++i];
        else if (a == L"--dll" && i + 1 < argc) dll = argv[++i];
        else if (a == L"--exe" && i + 1 < argc) exeOverride = argv[++i];
        else if (a == L"--harness" && i + 1 < argc) harness = argv[++i];
        else if (a == L"--wait") wait = true;
        else if (a == L"--no-hooks") SetEnvironmentVariableW(L"EI_NO_HOOKS", L"1");
        else if (a == L"--rebirth" && i + 1 < argc) SetEnvironmentVariableW(L"EI_REBIRTH", argv[++i]);
        else if (a == L"--out" && i + 1 < argc) outDir = argv[++i];
        else if (a == L"--standalone") standalone = true;
        else gameArgs += L" \"" + a + L"\"";
    }
    wchar_t full[MAX_PATH];
    GetFullPathNameW(dll.c_str(), MAX_PATH, full, nullptr);
    dll = full;
    if (outDir.empty()) {
        outDir = Dir(dll);
    } else {
        GetFullPathNameW(outDir.c_str(), MAX_PATH, full, nullptr);
        outDir = full;
        CreateDirectoryW(outDir.c_str(), nullptr);
        SetEnvironmentVariableW(L"EI_OUT_DIR", outDir.c_str());
    }

    // Rebirth keeps the engine in starter\Engine (with its addon loader next to it); a plain
    // install (e.g. GOG) has game.exe in the root.
    std::wstring exe = gameDir + L"\\starter\\Engine\\game.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) exe = gameDir + L"\\game.exe";
    if (!exeOverride.empty()) {
        // An exe outside the game folder (e.g. run\vanilla): tell ei_hook where the game data is,
        // so the game's registry path lookups point there instead of at another install.
        exe = exeOverride;
        SetEnvironmentVariableW(L"EI_GAME_ROOT", gameDir.c_str());
    }
    if (!harness.empty()) {
        wchar_t scenario[MAX_PATH];
        GetFullPathNameW(harness.c_str(), MAX_PATH, scenario, nullptr);
        if (GetFileAttributesW(scenario) == INVALID_FILE_ATTRIBUTES) {
            std::fwprintf(stderr, L"ei_launch: scenario %s not found\n", scenario);
            return 2;
        }
        SetEnvironmentVariableW(L"EI_HARNESS", scenario);
        wait = true;
    }
    // A report left over from an earlier run must not pass for this one.
    DeleteFileW((outDir + L"\\harness_quit").c_str());
    if (!harness.empty()) DeleteFileW((outDir + L"\\harness_report.txt").c_str());
    std::wstring cmd = L"\"" + exe + L"\"" + gameArgs;
    std::wstring program = exe;
    STARTUPINFOW si{sizeof(si)};
    BOOL inherit = FALSE;
    if (standalone) {
        GetFullPathNameW(exe.c_str(), MAX_PATH, full, nullptr);
        program = Dir(self) + L"\\ei_standalone.exe";
        cmd = L"\"" + program + L"\" --exe \"" + full + L"\" --dll \"" + dll + L"\" --run";
        if (!gameArgs.empty()) cmd += L" --" + gameArgs;
        // The host's own lines (imports it cannot resolve, its exit) go to ei_launch's output.
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        inherit = TRUE;
    }

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(program.c_str(), cmd.data(), nullptr, nullptr, inherit, CREATE_SUSPENDED, nullptr,
                        gameDir.c_str(), &si, &pi))
        return Fail(L"CreateProcess");

    // A run that waits for the game puts it in a job that dies with ei_launch (also when ei_launch is
    // killed): no game outlives its launcher. A launch without --wait leaves the game running.
    HANDLE job = nullptr;
    if (!harness.empty() || wait) {
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
            !AssignProcessToJobObject(job, pi.hProcess)) {
            std::fwprintf(stderr, L"ei_launch: warning: the game could not be put in a job object (error %lu)\n", GetLastError());
            if (job) CloseHandle(job);
            job = nullptr;
        }
    }

    // The standalone host loads ei_hook.dll itself.
    if (!standalone) {
        if (const int failed = InjectDll(pi.hProcess, dll)) return failed;
    }

    ResumeThread(pi.hThread);
    std::wprintf(L"ei_launch: started %s (pid %lu) with %s\n", standalone ? L"ei_standalone.exe" : L"game.exe",
                 pi.dwProcessId, dll.c_str());
    CloseHandle(pi.hThread);
    if (!wait) {
        CloseHandle(pi.hProcess);
        return 0;
    }

    // Wait for the game, then report the harness result (harness_report.txt in the output folder). The
    // harness writes harness_quit when its scenario quits, after its report; a game still running 3 s
    // after that is hung on exit (Rebirth always is, after DestroyClassRegistry) and is terminated, so
    // no run needs Task Manager. The vanilla game exits within 0.4 s (2.5 s at most, 16 games at once).
    std::wstring quitMarker = outDir + L"\\harness_quit";
    ULONGLONG quitSeen = 0;
    const ULONGLONG started = GetTickCount64();
    ULONGLONG limit = kDefaultTimeLimitMs;
    if (!harness.empty()) {
        wchar_t scenario[MAX_PATH];
        GetFullPathNameW(harness.c_str(), MAX_PATH, scenario, nullptr);
        limit = ScenarioTimeLimitMs(scenario);
    }
    bool timedOut = false;
    bool unkillable = false;
    std::wstring reportFile = outDir + L"\\harness_report.txt";
    while (WaitForSingleObject(pi.hProcess, 500) == WAIT_TIMEOUT) {
        if (harness.empty()) continue;
        // Backstop for the harness's own time limit (the scenario's "timeout", default 60 s): the game's
        // watchdog thread ends the game at the limit; if it has not, because the game is hung so badly
        // that it cannot run, the limit plus a margin has passed and the game is ended from here.
        if (GetTickCount64() - started >= limit + kTimeoutMarginMs) {
            std::fwprintf(stderr, L"ei_launch: TIMEOUT: the game is still running %llu s after its start (the scenario's "
                                  L"time limit is %llu s); terminating it\n", (GetTickCount64() - started) / 1000, limit / 1000);
            timedOut = true;
            unkillable = !EndGame(pi.hProcess, job, pi.dwProcessId, 3);
            break;
        }
        // The harness writes its report as soon as it loads; none after 30 s means it never started
        // (the game would otherwise sit in the main menu until someone closes it).
        if (GetTickCount64() - started >= 30000 &&
            GetFileAttributesW(reportFile.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::fwprintf(stderr, L"ei_launch: the harness did not start in the game; terminating it\n");
            unkillable = !EndGame(pi.hProcess, job, pi.dwProcessId, 2);
            break;
        }
        if (GetFileAttributesW(quitMarker.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        if (!quitSeen) quitSeen = GetTickCount64();
        if (GetTickCount64() - quitSeen >= 3000) {
            std::wprintf(L"ei_launch: game still running 3 s after the scenario quit; terminating it\n");
            unkillable = !EndGame(pi.hProcess, job, pi.dwProcessId, 0);
            break;
        }
    }
    DeleteFileW(quitMarker.c_str());
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (job) CloseHandle(job);   // ends whatever the game left running
    if (harness.empty()) return static_cast<int>(code);
    std::wstring reportPath = outDir + L"\\harness_report.txt";
    FILE* report = nullptr;
    if (_wfopen_s(&report, reportPath.c_str(), L"r") != 0 || !report) {
        std::fwprintf(stderr, L"ei_launch: game exited (code %lu) without a harness report\n", code);
        return 2;
    }
    char line[512];
    bool pass = false;
    while (std::fgets(line, sizeof(line), report)) {
        std::fputs(line, stdout);
        if (std::strncmp(line, "RESULT: PASS", 12) == 0) pass = true;
    }
    std::fclose(report);
    if (timedOut) return unkillable ? kExitUnkillable : kExitTimeout;
    return pass ? 0 : 1;
}
