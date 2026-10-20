// ei_launch: starts the standalone image (--standalone: ei_standalone.exe, our code) or the original
// game.exe the same way the Rebirth starter does (exe from starter\Engine, working directory = game
// root), or <game>\game.exe for a plain install. Nothing in the game folder is modified.
//
// usage: ei_launch [--game <dir>] [--exe <game.exe outside the game dir>] [--dll <ei_hook.dll>]
//                  [--harness <scenario.txt>] [--wait] [--no-hooks] [--rebirth <spec>] [--out <dir>]
//                  [--standalone] [game args...]
// --out puts what the run writes (ei_hook.log, the harness report and saves, ...) in <dir> instead
// of next to ei_hook.dll (EI_OUT_DIR), so several runs can go at once.
// game.exe is always the original game (the original's harness: goldens, EI_VECTORS recording, addon's
// WideScreen frames): ei_hook.dll is loaded into it before the game's own code runs, so the harness
// runs, with EI_NO_HOOKS=1, so none of the rewritten functions is installed (InstallHooks installs
// only the vector recorders there, with EI_VECTORS=1). --no-hooks says the same and is kept for the
// tools' command lines; since S21e nothing starts game.exe with our rewrites.
// --rebirth turns Rebirth patch flags on in the rewritten code (EI_REBIRTH; include/ei/rebirth.h):
// "all", or patch names separated by commas, each optionally =0/=1.
// --standalone runs ei_standalone.exe (next to ei_launch.exe; src/standalone/host.cpp) instead of game.exe:
// it lays out game.exe's data without its code and loads ei_hook.dll itself (--run), with the same
// environment, working directory, output folder, job object and time limits as a game.exe run. Its own
// output (imports, the INT3 hit) comes through ei_launch's. Game args go after "--" (WinMain gets them as
// game.exe would). EI_LAUNCH_STANDALONE=1 in the environment does the same as --standalone (a test batch's
// environment reaches ei_launch when the scheduler's run_all does not know the install yet; S19).
//
// A run with --harness ends whatever state the game is in: the harness's watchdog thread ends it at the
// scenario's "timeout" (60 s unless the scenario says otherwise); if that thread cannot run, ei_launch ends
// it kTimeoutMargin later. The game runs in a job object that is killed with ei_launch, so it (and any
// process it started) never outlives ei_launch. Exit codes of a run: 0 passed, 1 failed, 2 no report /
// not started, 3 and 4 below.
#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::wstring Dir(const std::wstring& path) {
    return path.substr(0, path.find_last_of(L"\\/"));
}

static bool ReadAll(const std::wstring& path, std::vector<char>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (64 << 20);
    if (ok) {
        out.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD got = 0;
        ok = out.empty() || (ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size());
    }
    CloseHandle(h);
    return ok;
}

static bool SameFile(const std::wstring& a, const std::wstring& b) {
    std::vector<char> x, y;
    return ReadAll(a, x) && ReadAll(b, y) && x == y;
}

// Windows Firewall asks about every new program path that listens outside loopback, and each worktree builds
// its own ei_standalone.exe. A launch without a scenario (a game to play, which hosts on every address) starts
// one shared copy instead, in the resolved run folder that holds game.exe's install folder
// (run\standalone\ei_standalone.exe), so the firewall asks once. The copy is refreshed when the built host
// differs; while a running game still holds an older copy, the launch uses its own build's host. Harness runs
// start their build's own host: they bind on 127.0.0.1 only (WB14, tests/verify/harness_net.h), which the
// firewall never asks about, and runs of several worktrees' builds would keep replacing the shared copy.
static std::wstring SharedStandaloneHost(const std::wstring& built, const std::wstring& gameExe) {
    HANDLE h = CreateFileW(Dir(Dir(gameExe)).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return built;
    wchar_t resolved[MAX_PATH];
    const DWORD n = GetFinalPathNameByHandleW(h, resolved, MAX_PATH, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (n == 0 || n >= MAX_PATH) return built;
    std::wstring dir = resolved;
    if (dir.rfind(L"\\\\?\\", 0) == 0 && dir.rfind(L"\\\\?\\UNC\\", 0) != 0) dir = dir.substr(4);
    dir += L"\\standalone";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring shared = dir + L"\\ei_standalone.exe";
    if (SameFile(built, shared)) return shared;
    const std::wstring temp = shared + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    if (CopyFileW(built.c_str(), temp.c_str(), FALSE) && MoveFileExW(temp.c_str(), shared.c_str(), MOVEFILE_REPLACE_EXISTING))
        return shared;
    DeleteFileW(temp.c_str());
    return SameFile(built, shared) ? shared : built;
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

// Where a game that will not end waits: for each of its threads, the instruction pointer and the return
// addresses on its stack (words after a CALL into a loaded module), as module+offset, written to <path>.
// The game's own watchdog cannot do this once exit() is under way (ExitProcess has ended every other thread).
static void WriteThreadStacks(HANDLE process, DWORD pid, const std::wstring& path) {
    struct Module {
        std::uintptr_t base, size;
        wchar_t name[MAX_MODULE_NAME32 + 1];
    };
    std::vector<Module> modules;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me{sizeof(me)};
        for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
            Module m{reinterpret_cast<std::uintptr_t>(me.modBaseAddr), me.modBaseSize, L""};
            wcscpy_s(m.name, me.szModule);
            modules.push_back(m);
        }
        CloseHandle(snap);
    }
    auto describe = [&](std::uintptr_t a, wchar_t* out, std::size_t size) {
        for (const Module& m : modules)
            if (a - m.base < m.size) {
                std::swprintf(out, size, L"%s+%X", m.name, static_cast<unsigned>(a - m.base));
                return true;
            }
        return false;
    };
    // A return address: the 2, 3, 5 or 6 bytes before it are a CALL (FF /2 register or memory, E8 rel32).
    auto afterCall = [&](std::uintptr_t a) {
        unsigned char b[8] = {};
        SIZE_T got = 0;
        if (!ReadProcessMemory(process, reinterpret_cast<void*>(a - 8), b, 8, &got) || got != 8) return false;
        if (b[3] == 0xE8) return true;
        for (int n : {2, 3, 6, 7})
            if (b[8 - n] == 0xFF && ((b[9 - n] >> 3) & 7) == 2) return true;
        return false;
    };
    FILE* out = nullptr;
    if (_wfopen_s(&out, path.c_str(), L"w") != 0 || !out) return;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof(te)};
    for (BOOL ok = snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!t) continue;
        SuspendThread(t);
        CONTEXT c{};
        c.ContextFlags = CONTEXT_CONTROL;
        wchar_t where[MAX_MODULE_NAME32 + 16] = L"?";
        if (GetThreadContext(t, &c)) {
            describe(c.Eip, where, std::size(where));
            std::fwprintf(out, L"thread %lu: eip %08lX %s\n", te.th32ThreadID, c.Eip, where);
            std::vector<std::uint32_t> stack(16384);
            SIZE_T got = 0;
            ReadProcessMemory(process, reinterpret_cast<void*>(c.Esp), stack.data(), stack.size() * 4, &got);
            if (!got) {
                // The read stops at the stack's end: take what is there, a page at a time.
                for (SIZE_T n = 4096; n <= stack.size() * 4; n += 4096) {
                    SIZE_T part = 0;
                    if (!ReadProcessMemory(process, reinterpret_cast<void*>(c.Esp), stack.data(), n, &part)) break;
                    got = part;
                }
            }
            int shown = 0;
            for (SIZE_T i = 0; i < got / 4 && shown < 48; ++i)
                if (describe(stack[i], where, std::size(where)) && afterCall(stack[i])) {
                    std::fwprintf(out, L"  %s\n", where);
                    ++shown;
                }
        }
        ResumeThread(t);
        CloseHandle(t);
    }
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
    std::fclose(out);
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

// ---- A pair of games (SV5d; tests/verify/harness_pair.h) ------------------------------------------------
//
// A scenario's "peer <name>" line makes this launch start a second game beside the scenario's own, on
// tests/scenarios/peer/<name>.txt (next to the scenario's folder), the same program, writing to <out>\peer.
// Each gets EI_PAIR=<pair name>,<side> (0 the scenario's game, 1 the peer): the harness puts the two in
// lockstep on a network of their own. Both are in this launch's job object and are ended together when either
// passes its limit or never starts its harness. The peer's result goes into the scenario's report (a failed
// peer fails the run, as "FAILED: peer: ..." lines), so run_all reads one result per pair.
//
// A pair is two games against the machine's game slots (run_all.py MACHINE_SLOTS, byte locks on files in
// <temp>\ei_harness_slots): unless the caller says it holds both (EI_PAIR_SLOT_HELD=1, run_all.py since SV5d),
// the launch takes a second slot itself before starting either game, as run_all's machine_slot() does.

// The scenario's "peer <name>" line: the peer's scenario path, "" without one.
static std::wstring PeerScenario(const wchar_t* scenario) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, scenario, L"r") != 0 || !f) return L"";
    char line[1024];
    std::string name;
    while (std::fgets(line, sizeof(line), f)) {
        const char* p = line + std::strspn(line, " \t");
        if (std::strncmp(p, "peer", 4) != 0 || (p[4] != ' ' && p[4] != '\t')) continue;
        name = p + 5;
        name.erase(0, name.find_first_not_of(" \t"));
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' ')) name.pop_back();
    }
    std::fclose(f);
    if (name.empty()) return L"";
    return Dir(scenario) + L"\\peer\\" + std::wstring(name.begin(), name.end()) + L".txt";
}

// The one-byte lock on a slot file (as run_all.py's _lock: byte 0, without waiting), or nullptr.
static HANDLE LockSlotFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    OVERLAPPED at{};
    if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &at)) {
        CloseHandle(h);
        return nullptr;
    }
    return h;
}

// run_all.py's SLOT_DIR: Python's tempfile.gettempdir() (TMPDIR, TEMP, TMP) plus ei_harness_slots.
static std::wstring SlotDir() {
    wchar_t value[MAX_PATH];
    for (const wchar_t* name : {L"TMPDIR", L"TEMP", L"TMP"}) {
        const DWORD n = GetEnvironmentVariableW(name, value, MAX_PATH);
        if (n > 0 && n < MAX_PATH) return std::wstring(value) + L"\\ei_harness_slots";
    }
    GetTempPathW(MAX_PATH, value);
    return std::wstring(value) + L"ei_harness_slots";
}

// One of the machine's game slots for the peer game, waiting up to 90 s for one (a game that wants every slot
// holds the "alone" gate: no slot is taken meanwhile, as in machine_slot()). The lock is held until the
// handle closes (at exit). nullptr when none came.
static HANDLE TakeMachineSlot() {
    const std::wstring dir = SlotDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    wchar_t value[16];
    int slots = 16;
    if (GetEnvironmentVariableW(L"EI_HARNESS_SLOTS", value, 16) > 0) slots = _wtoi(value);
    const ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start < 90000) {
        if (HANDLE gate = LockSlotFile(dir + L"\\alone")) {
            CloseHandle(gate);
            for (int i = 0; i < slots; ++i) {
                wchar_t file[32];
                std::swprintf(file, 32, L"\\slot%02d", i);
                if (HANDLE slot = LockSlotFile(dir + file)) {
                    std::wprintf(L"ei_launch: machine slot %d taken for the peer game\n", i);
                    return slot;
                }
            }
        }
        Sleep(500);
    }
    return nullptr;
}

// One game of a run: the scenario's own, or the peer.
struct Game {
    std::wstring scenario, outDir, label;
    PROCESS_INFORMATION pi{};
    ULONGLONG limit = 0;
    ULONGLONG quitSeen = 0;
    bool running = false;
    DWORD code = 0;
};

// The report's lines (empty when there is none).
static bool ReadReport(const std::wstring& path, std::vector<std::string>& lines) {
    FILE* report = nullptr;
    if (_wfopen_s(&report, path.c_str(), L"r") != 0 || !report) return false;
    char line[512];
    while (std::fgets(line, sizeof(line), report)) lines.push_back(line);
    std::fclose(report);
    return true;
}

static void WriteReport(const std::wstring& path, const std::vector<std::string>& lines) {
    FILE* rewrite = nullptr;
    if (_wfopen_s(&rewrite, path.c_str(), L"w") != 0 || !rewrite) return;
    for (const std::string& r : lines) std::fputs(r.c_str(), rewrite);
    std::fclose(rewrite);
}

// A game that ended by itself while its report still says RUNNING died before the harness could write a result
// (an INT3 of the standalone image before the missing-code report, a crash the harness did not see): the report
// gets the reason instead of a silent RUNNING (S16c).
static void ReportSilentEnd(const Game& game, bool standalone) {
    const std::wstring path = game.outDir + L"\\harness_report.txt";
    std::vector<std::string> lines;
    if (!ReadReport(path, lines)) return;
    for (std::string& l : lines) {
        if (l.compare(0, 15, "RESULT: RUNNING") != 0) continue;
        char why[256];
        if (game.code == static_cast<DWORD>(EXCEPTION_BREAKPOINT))
            std::snprintf(why, sizeof(why), "FAILED: the game ended on a breakpoint (exit code %08lX) before the harness "
                                            "wrote its result%s\n", game.code,
                          standalone ? ": an INT3 of the standalone image with no code there (ei_hook.log, the run notes)" : "");
        else
            std::snprintf(why, sizeof(why), "FAILED: the game ended (exit code %lu, %08lX) before the harness wrote its "
                                            "result\n", game.code, game.code);
        l = std::string(why) + "RESULT: FAIL\n";
        std::fwprintf(stderr, L"ei_launch: %s%hs", game.label.c_str(), why);
        WriteReport(path, lines);
        break;
    }
}

// The peer's result in the scenario's report: a peer that did not pass (or whose windows or network checks saw
// something) adds "FAILED: peer: ..." lines and makes the result FAIL.
static void MergePeerReport(const std::wstring& reportPath, const std::wstring& peerPath) {
    std::vector<std::string> peer, failures;
    if (!ReadReport(peerPath, peer)) {
        failures.push_back("FAILED: peer: no report (peer\\harness_report.txt)\n");
    } else {
        bool passed = false;
        for (const std::string& l : peer) {
            if (l.compare(0, 8, "FAILED: ") == 0) failures.push_back("FAILED: peer: " + l.substr(8));
            if (l.compare(0, 12, "RESULT: PASS") == 0) passed = true;
            if (l.compare(0, 8, "WINDOW: ") == 0 && l.compare(0, 20, "WINDOW: out of sight") != 0)
                failures.push_back("FAILED: peer: " + l);
            if (l.compare(0, 5, "NET: ") == 0 && l.compare(0, 18, "NET: loopback only") != 0)
                failures.push_back("FAILED: peer: " + l);
        }
        if (!passed && failures.empty()) failures.push_back("FAILED: peer: did not pass (peer\\harness_report.txt)\n");
    }
    std::wprintf(L"ei_launch: peer: %hs\n", failures.empty() ? "passed" : "did not pass");
    if (failures.empty()) return;
    std::vector<std::string> lines;
    ReadReport(reportPath, lines);
    std::vector<std::string> merged;
    bool resulted = false;
    for (const std::string& l : lines) {
        if (l.compare(0, 8, "RESULT: ") == 0) {
            merged.insert(merged.end(), failures.begin(), failures.end());
            merged.push_back("RESULT: FAIL\n");
            resulted = true;
        } else {
            merged.push_back(l);
        }
    }
    if (!resulted) {
        merged.insert(merged.end(), failures.begin(), failures.end());
        merged.push_back("RESULT: FAIL\n");
    }
    WriteReport(reportPath, merged);
}

// The machine's two installs: the default game folder is Rebirth's (run_all passes --game for the vanilla one).
constexpr const wchar_t* kVanillaInstall = L"F:\\games\\EvilIslands";
constexpr const wchar_t* kRebirthInstall = L"F:\\games\\Evil Islands (Rebirth)";

int wmain(int argc, wchar_t** argv) {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);

    std::wstring gameDir = kRebirthInstall;
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
        else if (a == L"--no-hooks") continue;   // game.exe always runs without hooks (below)
        else if (a == L"--rebirth" && i + 1 < argc) SetEnvironmentVariableW(L"EI_REBIRTH", argv[++i]);
        else if (a == L"--out" && i + 1 < argc) outDir = argv[++i];
        else if (a == L"--standalone") standalone = true;
        else gameArgs += L" \"" + a + L"\"";
    }
    wchar_t standaloneValue[4];
    if (GetEnvironmentVariableW(L"EI_LAUNCH_STANDALONE", standaloneValue, 4) == 1 && standaloneValue[0] == L'1') standalone = true;
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
        // The installs no harness run may write in, whichever it plays (tests/verify/harness_install.h).
        SetEnvironmentVariableW(L"EI_INSTALL_ROOTS", (gameDir + L";" + kVanillaInstall + L";" + kRebirthInstall).c_str());
        wait = true;
    }
    // The games: the scenario's own, and with a "peer" line the pair's other game (above).
    std::vector<Game> games(1);
    games[0].outDir = outDir;
    if (!harness.empty()) {
        wchar_t scenario[MAX_PATH];
        GetFullPathNameW(harness.c_str(), MAX_PATH, scenario, nullptr);
        games[0].scenario = scenario;
        if (const std::wstring peer = PeerScenario(scenario); !peer.empty()) {
            if (GetFileAttributesW(peer.c_str()) == INVALID_FILE_ATTRIBUTES) {
                std::fwprintf(stderr, L"ei_launch: the peer's scenario %s not found\n", peer.c_str());
                return 2;
            }
            Game other;
            other.scenario = peer;
            other.outDir = outDir + L"\\peer";
            other.label = L"peer: ";
            CreateDirectoryW(other.outDir.c_str(), nullptr);
            games.push_back(other);
        }
    }
    for (Game& game : games) game.limit = game.scenario.empty() ? kDefaultTimeLimitMs : ScenarioTimeLimitMs(game.scenario.c_str());
    HANDLE peerSlot = nullptr;
    if (games.size() > 1) {
        wchar_t held[4];
        if (!(GetEnvironmentVariableW(L"EI_PAIR_SLOT_HELD", held, 4) == 1 && held[0] == L'1') &&
            !(peerSlot = TakeMachineSlot())) {
            std::fwprintf(stderr, L"ei_launch: no machine slot for the peer game in 90 s; not started\n");
            return 2;
        }
    }
    // A report left over from an earlier run must not pass for this one.
    for (const Game& game : games) {
        DeleteFileW((game.outDir + L"\\harness_quit").c_str());
        if (!harness.empty()) DeleteFileW((game.outDir + L"\\harness_report.txt").c_str());
    }
    std::wstring cmd = L"\"" + exe + L"\"" + gameArgs;
    std::wstring program = exe;
    STARTUPINFOW si{sizeof(si)};
    BOOL inherit = FALSE;
    if (standalone) {
        GetFullPathNameW(exe.c_str(), MAX_PATH, full, nullptr);
        const std::wstring built = Dir(self) + L"\\ei_standalone.exe";
        program = harness.empty() ? SharedStandaloneHost(built, full) : built;
        cmd = L"\"" + program + L"\" --exe \"" + full + L"\" --dll \"" + dll + L"\" --run";
        if (!gameArgs.empty()) cmd += L" --" + gameArgs;
        // The host's own lines (imports it cannot resolve, its exit) go to ei_launch's output.
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        inherit = TRUE;
    }

    // game.exe runs the original code only (the harness's leftover patches follow EI_NO_HOOKS too); the
    // image never has it.
    SetEnvironmentVariableW(L"EI_NO_HOOKS", standalone ? nullptr : L"1");

    // The original game on the vanilla data with Rebirth's bug fixes (SV9, tests/verify/harness_addon.h): a harness
    // run of game.exe there also loads Rebirth's addon loader (starter\Engine\dinput.dll, which loads addon.dll at
    // the game's entry point) and the harness keeps only the patches of the run's Rebirth flags (the standalone's
    // defaults). Not for function vectors (EI_VECTORS=1: the plain original's) or with EI_ADDON_FLAGS=0.
    std::wstring addonLoader;
    {
        wchar_t vectors[4] = L"", flags[4] = L"";
        GetEnvironmentVariableW(L"EI_VECTORS", vectors, 4);
        GetEnvironmentVariableW(L"EI_ADDON_FLAGS", flags, 4);
        wchar_t fullGame[MAX_PATH];
        GetFullPathNameW(gameDir.c_str(), MAX_PATH, fullGame, nullptr);
        std::wstring game = fullGame;
        while (game.size() > 3 && game.back() == L'\\') game.pop_back();
        if (!standalone && !harness.empty() && _wcsicmp(game.c_str(), kVanillaInstall) == 0 && wcscmp(vectors, L"1") != 0 &&
            wcscmp(flags, L"0") != 0) {
            addonLoader = std::wstring(kRebirthInstall) + L"\\starter\\Engine\\dinput.dll";
            if (GetFileAttributesW(addonLoader.c_str()) == INVALID_FILE_ATTRIBUTES) {
                std::fwprintf(stderr, L"ei_launch: Rebirth's addon loader %s not found\n", addonLoader.c_str());
                return 2;
            }
            SetEnvironmentVariableW(L"EI_ADDON_FLAGS", L"1");
        } else if (!standalone) {
            SetEnvironmentVariableW(L"EI_ADDON_FLAGS", nullptr);
        }
    }

    // A run that waits for the game puts it in a job that dies with ei_launch (also when ei_launch is
    // killed): no game outlives its launcher. A launch without --wait leaves the game running.
    HANDLE job = nullptr;
    if (!harness.empty() || wait) {
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            std::fwprintf(stderr, L"ei_launch: warning: no job object for the game (error %lu)\n", GetLastError());
            if (job) CloseHandle(job);
            job = nullptr;
        }
    }

    // A pair's name: unique on the machine (the launch's process id and start time).
    wchar_t pairName[64];
    std::swprintf(pairName, 64, L"ei_pair_%lu_%llu", GetCurrentProcessId(), GetTickCount64());
    for (std::size_t side = 0; side < games.size(); ++side) {
        Game& game = games[side];
        if (games.size() > 1) {
            SetEnvironmentVariableW(L"EI_PAIR", (std::wstring(pairName) + L"," + std::to_wstring(side)).c_str());
            SetEnvironmentVariableW(L"EI_HARNESS", game.scenario.c_str());
            if (side > 0) SetEnvironmentVariableW(L"EI_OUT_DIR", game.outDir.c_str());
        }
        std::wstring line = cmd;
        if (!CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, inherit, CREATE_SUSPENDED, nullptr,
                            gameDir.c_str(), &si, &game.pi)) {
            for (Game& started : games)
                if (started.running) EndGame(started.pi.hProcess, job, started.pi.dwProcessId, 1);
            return Fail(L"CreateProcess");
        }
        game.running = true;
        if (job && !AssignProcessToJobObject(job, game.pi.hProcess))
            std::fwprintf(stderr, L"ei_launch: warning: the game could not be put in the job object (error %lu)\n",
                          GetLastError());
        // The standalone host loads ei_hook.dll itself; game.exe gets it before its own code runs.
        if (!standalone) {
            if (const int failed = InjectDll(game.pi.hProcess, dll)) {
                for (Game& started : games)
                    if (started.running) EndGame(started.pi.hProcess, job, started.pi.dwProcessId, 1);
                return failed;
            }
            // After ei_hook.dll, whose harness answers the loader's registry reads (harness_settings.h).
            if (!addonLoader.empty())
                if (const int failed = InjectDll(game.pi.hProcess, addonLoader)) {
                    for (Game& started : games)
                        if (started.running) EndGame(started.pi.hProcess, job, started.pi.dwProcessId, 1);
                    return failed;
                }
        }
    }
    for (Game& game : games) {
        ResumeThread(game.pi.hThread);
        std::wprintf(L"ei_launch: started %s%s (pid %lu) with %s\n", game.label.c_str(),
                     standalone ? L"ei_standalone.exe" : L"game.exe", game.pi.dwProcessId, dll.c_str());
        CloseHandle(game.pi.hThread);
    }
    if (!wait) {
        for (Game& game : games) CloseHandle(game.pi.hProcess);
        return 0;
    }

    // Wait for the game, then report the harness result (harness_report.txt in the output folder). The
    // harness writes harness_quit when its scenario quits, after its report; a game still running 3 s
    // after that is hung on exit and is terminated, so no run needs Task Manager; where its threads were
    // goes to exit_hang.txt first. The vanilla game exits within 0.4 s (2.5 s at most, 16 games at once).
    // Rebirth runs hung in addon.dll's DLL_PROCESS_DETACH (restarting CTFMon.exe) until the harness turned
    // its CtfMon switch off (S3e, tests/verify/harness_starter.cpp).
    // A pair's games are watched alike; a game past its limit, or whose harness never started, ends both
    // (the other would only wait for it).
    const ULONGLONG started = GetTickCount64();
    bool timedOut = false;
    bool unkillable = false;
    auto endAll = [&](UINT code) {
        for (Game& other : games)
            if (other.running) {
                unkillable = !EndGame(other.pi.hProcess, job, other.pi.dwProcessId, code) || unkillable;
                other.running = false;
            }
    };
    for (;;) {
        std::vector<HANDLE> alive;
        for (Game& game : games)
            if (game.running) alive.push_back(game.pi.hProcess);
        if (alive.empty()) break;
        WaitForMultipleObjects(static_cast<DWORD>(alive.size()), alive.data(), FALSE, 500);
        for (Game& game : games) {
            if (!game.running) continue;
            if (WaitForSingleObject(game.pi.hProcess, 0) == WAIT_OBJECT_0) {
                game.running = false;
                continue;
            }
            if (harness.empty()) continue;
            // Backstop for the harness's own time limit (the scenario's "timeout", default 60 s): the game's
            // watchdog thread ends the game at the limit; if it has not, because the game is hung so badly
            // that it cannot run, the limit plus a margin has passed and the game is ended from here.
            if (GetTickCount64() - started >= game.limit + kTimeoutMarginMs) {
                std::fwprintf(stderr, L"ei_launch: TIMEOUT: the %sgame is still running %llu s after its start (the "
                                      L"scenario's time limit is %llu s); terminating it%s\n", game.label.c_str(),
                              (GetTickCount64() - started) / 1000, game.limit / 1000, games.size() > 1 ? L" and the other" : L"");
                timedOut = true;
                endAll(3);
                break;
            }
            // The harness writes its report as soon as it loads; none after 30 s means it never started
            // (the game would otherwise sit in the main menu until someone closes it).
            if (GetTickCount64() - started >= 30000 &&
                GetFileAttributesW((game.outDir + L"\\harness_report.txt").c_str()) == INVALID_FILE_ATTRIBUTES) {
                std::fwprintf(stderr, L"ei_launch: the harness did not start in the %sgame; terminating it%s\n",
                              game.label.c_str(), games.size() > 1 ? L" and the other" : L"");
                endAll(2);
                break;
            }
            // The harness writes harness_quit when its scenario quits, after its report; a game still running 3 s
            // after that is hung on exit and is terminated, so no run needs Task Manager; where its threads were
            // goes to exit_hang.txt first. The vanilla game exits within 0.4 s (2.5 s at most, 16 games at once).
            // Rebirth runs hung in addon.dll's DLL_PROCESS_DETACH (restarting CTFMon.exe) until the harness turned
            // its CtfMon switch off (S3e, tests/verify/harness_starter.cpp).
            if (GetFileAttributesW((game.outDir + L"\\harness_quit").c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            if (!game.quitSeen) game.quitSeen = GetTickCount64();
            if (GetTickCount64() - game.quitSeen >= 3000) {
                WriteThreadStacks(game.pi.hProcess, game.pi.dwProcessId, game.outDir + L"\\exit_hang.txt");
                std::wprintf(L"ei_launch: %sgame still running 3 s after the scenario quit (its threads: exit_hang.txt); "
                             L"terminating it\n", game.label.c_str());
                unkillable = !EndGame(game.pi.hProcess, games.size() > 1 ? nullptr : job, game.pi.dwProcessId, 0) || unkillable;
                game.running = false;
            }
        }
    }
    for (Game& game : games) {
        DeleteFileW((game.outDir + L"\\harness_quit").c_str());
        GetExitCodeProcess(game.pi.hProcess, &game.code);
        CloseHandle(game.pi.hProcess);
    }
    if (job) CloseHandle(job);   // ends whatever the games left running
    if (peerSlot) CloseHandle(peerSlot);
    DWORD code = games[0].code;
    if (harness.empty()) return static_cast<int>(code);
    if (!timedOut && !unkillable)
        for (const Game& game : games)
            if (!game.quitSeen) ReportSilentEnd(game, standalone);
    const std::wstring reportPath = outDir + L"\\harness_report.txt";
    if (GetFileAttributesW(reportPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::fwprintf(stderr, L"ei_launch: game exited (code %lu) without a harness report\n", code);
        return 2;
    }
    if (games.size() > 1) MergePeerReport(reportPath, games[1].outDir + L"\\harness_report.txt");
    bool pass = false;
    std::vector<std::string> lines;
    ReadReport(reportPath, lines);
    for (const std::string& l : lines) {
        std::fputs(l.c_str(), stdout);
        if (l.compare(0, 12, "RESULT: PASS") == 0) pass = true;
    }
    if (timedOut) return unkillable ? kExitUnkillable : kExitTimeout;
    return pass ? 0 : 1;
}
