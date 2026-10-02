// ei_launch: starts the original game.exe the same way the Rebirth starter does (exe from
// starter\Engine, working directory = game root), or <game>\game.exe for a plain install, and loads
// ei_hook.dll into it before the game's own code runs. Nothing in the game folder is modified.
//
// usage: ei_launch [--game <dir>] [--exe <game.exe outside the game dir>] [--dll <ei_hook.dll>]
//                  [--harness <scenario.txt>] [--wait] [game args...]
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

static std::wstring Dir(const std::wstring& path) {
    return path.substr(0, path.find_last_of(L"\\/"));
}

static int Fail(const wchar_t* what) {
    std::fwprintf(stderr, L"ei_launch: %s failed (error %lu)\n", what, GetLastError());
    return 1;
}

int wmain(int argc, wchar_t** argv) {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);

    std::wstring gameDir = L"F:\\games\\Evil Islands (Rebirth)";
    std::wstring dll = Dir(self) + L"\\ei_hook.dll";
    std::wstring exeOverride;
    std::wstring harness;
    bool wait = false;
    std::wstring gameArgs;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--game" && i + 1 < argc) gameDir = argv[++i];
        else if (a == L"--dll" && i + 1 < argc) dll = argv[++i];
        else if (a == L"--exe" && i + 1 < argc) exeOverride = argv[++i];
        else if (a == L"--harness" && i + 1 < argc) harness = argv[++i];
        else if (a == L"--wait") wait = true;
        else gameArgs += L" \"" + a + L"\"";
    }
    wchar_t full[MAX_PATH];
    GetFullPathNameW(dll.c_str(), MAX_PATH, full, nullptr);
    dll = full;

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
        SetEnvironmentVariableW(L"EI_HARNESS", scenario);
        wait = true;
    }
    std::wstring cmd = L"\"" + exe + L"\"" + gameArgs;

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                        gameDir.c_str(), &si, &pi))
        return Fail(L"CreateProcess");

    // Load the DLL with a remote LoadLibraryW call. ei_launch is 32-bit like the game, so
    // kernel32's address is the same in both processes.
    std::size_t bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(pi.hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(pi.hProcess, remote, dll.c_str(), bytes, nullptr)) {
        TerminateProcess(pi.hProcess, 1);
        return Fail(L"writing DLL path");
    }
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(pi.hProcess, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (!thread) {
        TerminateProcess(pi.hProcess, 1);
        return Fail(L"CreateRemoteThread");
    }
    WaitForSingleObject(thread, INFINITE);
    DWORD module = 0;
    GetExitCodeThread(thread, &module);
    CloseHandle(thread);
    VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
    if (!module) {
        TerminateProcess(pi.hProcess, 1);
        std::fwprintf(stderr, L"ei_launch: LoadLibrary of %s failed inside the game\n", dll.c_str());
        return 1;
    }

    ResumeThread(pi.hThread);
    std::wprintf(L"ei_launch: started game.exe (pid %lu) with %s\n", pi.dwProcessId, dll.c_str());
    CloseHandle(pi.hThread);
    if (!wait) {
        CloseHandle(pi.hProcess);
        return 0;
    }

    // Wait for the game, then report the harness result (harness_report.txt next to the DLL).
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (harness.empty()) return static_cast<int>(code);
    std::wstring reportPath = Dir(dll) + L"\\harness_report.txt";
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
    return pass ? 0 : 1;
}
