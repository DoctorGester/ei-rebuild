// CMake's compiler launcher of the Windows builds (tools/compiler_cache.py builds it; docs/notes/standalone.md,
// "The compiler cache"). Ninja runs every compile through it:
//
//   compiler_cache_launcher.exe NAME=VALUE... -- COMPILER ARGS...
//
// The NAME=VALUE pairs go into the environment (sccache's server variables). EI_CACHE_PROGRAM names sccache.exe,
// EI_CACHE_MAIN the main checkout's folder and EI_CACHE_HERE this checkout's. When build.cmd sets
// EI_COMPILER_CACHE_DIRECT=1 (a build folder's later builds), the compiler runs by itself. Otherwise it runs through
// sccache.
//
// The compiler's output is passed on with every other checkout's folder in it changed to this one's. A cached object
// comes with the output of the checkout that compiled it first, so its /showIncludes lines name that checkout's
// headers. Ninja keeps those lines as the object's dependencies (deps = msvc). Left as they are, a header changed
// here would rebuild nothing.
#include <windows.h>

#include <cstdio>
#include <cwctype>
#include <string>

namespace {

std::wstring Env(const wchar_t* name) {
    wchar_t buffer[32768];
    const DWORD n = GetEnvironmentVariableW(name, buffer, 32768);
    return n > 0 && n < 32768 ? std::wstring(buffer, n) : std::wstring();
}

bool IsSeparator(char c) { return c == '\\' || c == '/'; }

// The length of `prefix` at text[at], comparing letters without case and either slash as a separator; 0 if absent.
size_t Match(const std::string& text, size_t at, const std::string& prefix) {
    if (at + prefix.size() > text.size()) {
        return 0;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        const char a = text[at + i];
        const char b = prefix[i];
        if (IsSeparator(a) && IsSeparator(b)) {
            continue;
        }
        if (std::towlower(static_cast<unsigned char>(a)) != std::towlower(static_cast<unsigned char>(b))) {
            return 0;
        }
    }
    return prefix.size();
}

// The length of another checkout's folder, with its separator, at text[at]: a worktree's (main\.claude\worktrees\
// <name>\) or the main checkout's (main\), except main\.claude\ and main\build\third_party\, which no checkout owns.
size_t OtherCheckout(const std::string& text, size_t at, const std::string& main) {
    size_t n = Match(text, at, main);
    if (n == 0 || at + n >= text.size() || !IsSeparator(text[at + n])) {
        return 0;
    }
    ++n;
    if (const size_t w = Match(text, at + n, ".claude\\worktrees\\")) {
        size_t end = at + n + w;
        while (end < text.size() && !IsSeparator(text[end]) && text[end] != '\r' && text[end] != '\n') {
            ++end;
        }
        return end < text.size() && IsSeparator(text[end]) ? end + 1 - at : 0;
    }
    if (Match(text, at + n, ".claude\\") || Match(text, at + n, "build\\third_party\\")) {
        return 0;
    }
    return n;
}

std::string Narrow(const std::wstring& text) {
    const int n = WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr,
                                      nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string Rewrite(const std::string& text, const std::string& main, const std::string& here) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (const size_t n = OtherCheckout(text, i, main)) {
            out += here;
            out += '\\';
            i += n;
        } else {
            out += text[i++];
        }
    }
    return out;
}

int Fail(const char* what) {
    std::fprintf(stderr, "compiler_cache_launcher: %s (error %lu)\n", what, GetLastError());
    return 1;
}

}  // namespace

int main() {
    // The arguments as written: the compiler's are passed on unchanged after " -- ".
    std::wstring line = GetCommandLineW();
    const size_t dashes = line.find(L" -- ");
    if (dashes == std::wstring::npos) {
        std::fprintf(stderr, "compiler_cache_launcher: no -- before the compiler\n");
        return 1;
    }
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(line.substr(0, dashes).c_str(), &argc);
    for (int i = 1; i < argc; ++i) {
        const std::wstring pair = argv[i];
        const size_t eq = pair.find(L'=');
        if (eq != std::wstring::npos) {
            SetEnvironmentVariableW(pair.substr(0, eq).c_str(), pair.substr(eq + 1).c_str());
        }
    }
    LocalFree(argv);
    const std::wstring compile = line.substr(dashes + 4);
    std::wstring command = Env(L"EI_COMPILER_CACHE_DIRECT") == L"1"
                               ? compile
                               : L"\"" + Env(L"EI_CACHE_PROGRAM") + L"\" " + compile;

    // The child's output through a pipe that only it inherits.
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    if (!CreatePipe(&read, &write, &inherit, 0)) {
        return Fail("no pipe");
    }
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    HANDLE handles[3] = {write, GetStdHandle(STD_ERROR_HANDLE), GetStdHandle(STD_INPUT_HANDLE)};
    DWORD count = 1;
    for (DWORD i = 1; i < 3; ++i) {
        if (handles[i] && handles[i] != INVALID_HANDLE_VALUE &&
            SetHandleInformation(handles[i], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
            handles[count++] = handles[i];
        }
    }
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::string attributes(size, '\0');
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &size) ||
        !UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, count * sizeof(HANDLE),
                                   nullptr, nullptr)) {
        return Fail("no handle list");
    }
    STARTUPINFOEXW start{};
    start.StartupInfo.cb = sizeof(start);
    start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    start.StartupInfo.hStdOutput = write;
    start.StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    start.StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    start.lpAttributeList = list;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT, nullptr,
                        nullptr, &start.StartupInfo, &process)) {
        return Fail("the compiler did not start");
    }
    CloseHandle(write);
    DeleteProcThreadAttributeList(list);

    std::string output;
    char buffer[65536];
    DWORD got = 0;
    while (ReadFile(read, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
        output.append(buffer, got);
    }
    CloseHandle(read);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);

    output = Rewrite(output, Narrow(Env(L"EI_CACHE_MAIN")), Narrow(Env(L"EI_CACHE_HERE")));
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    for (size_t done = 0; done < output.size();) {
        DWORD wrote = 0;
        if (!WriteFile(out, output.data() + done, static_cast<DWORD>(output.size() - done), &wrote, nullptr)) {
            break;
        }
        done += wrote;
    }
    return static_cast<int>(code);
}
