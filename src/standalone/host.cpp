// ei_standalone: runs the rebuilt game without game.exe. See docs/notes/standalone.md, "The image's source files".
// usage: ei_standalone [--dll <ei_hook.dll>] [--run] [-- <game arguments>]
//   The game folder is the exe's own (under ei_launch: the working directory it starts the host in). Without
//   --run the host builds the image, takes the registrations, checks them and exits: no window, nothing of the
//   game runs. What follows "--" is the game's command line, as typed (WinMain's).

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int Fail(const char* what) {
    std::fprintf(stderr, "ei_standalone: %s (error %lu)\n", what, GetLastError());
    return 1;
}

std::string Folder(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// The game's arguments: what follows the token "--" on the host's command line, as typed.
std::string GameArguments() {
    const char* p = GetCommandLineA();
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* token = p;
        bool quoted = false;
        for (; *p && (quoted || (*p != ' ' && *p != '\t')); ++p)
            if (*p == '"') quoted = !quoted;
        if (p - token == 2 && token[0] == '-' && token[1] == '-') {
            while (*p == ' ' || *p == '\t') ++p;
            return p;
        }
    }
    return {};
}

} // namespace

// Marks this process as the standalone image for ei_hook.dll (ei::Standalone): game.exe exports nothing.
extern "C" __declspec(dllexport) int EiStandaloneHost = 1;

int main(int argc, char** argv) {
    // Under ei_launch --standalone stdout is a pipe: unbuffered, so a run that ends on a missing-code report
    // (TerminateProcess) loses none of it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string dll = Folder(self) + "\\ei_hook.dll";
    bool run = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--dll") && i + 1 < argc) dll = argv[++i];
        else if (!std::strcmp(argv[i], "--run")) run = true;
        else if (!std::strcmp(argv[i], "--")) break;
        else {
            std::fprintf(stderr, "usage: ei_standalone [--dll <ei_hook.dll>] [--run] [-- <game arguments>]\n");
            return 2;
        }
    }
    // The game folder is the working directory: the game's paths (Work path and CD-ROM Path ".\", ".\res\", the
    // saves) and its DLLs (image_imports.cpp) are found there. ei_launch starts the host in the data folder it names
    // (EI_GAME_ROOT); otherwise the exe is the drop-in one in the game folder, started from anywhere (a
    // shortcut's "Start in", a shell in another folder): its own folder.
    if (GetEnvironmentVariableA("EI_GAME_ROOT", nullptr, 0) == 0) {
        char full[MAX_PATH];
        const DWORD n = GetFullPathNameA(dll.c_str(), MAX_PATH, full, nullptr);
        if (n > 0 && n < MAX_PATH) dll = full;
        if (!SetCurrentDirectoryA(Folder(self).c_str())) return Fail(("cannot enter " + Folder(self)).c_str());
    }
    HMODULE hook = LoadLibraryA(dll.c_str());
    if (!hook) return Fail(("cannot load " + dll).c_str());
    using Main = int(__cdecl*)(int run, const char* arguments);
    auto start = reinterpret_cast<Main>(GetProcAddress(hook, "EiStandaloneMain"));
    if (!start) return Fail("ei_hook.dll has no EiStandaloneMain");
    const int result = start(run ? 1 : 0, GameArguments().c_str());
    std::printf("ei_standalone: %s %d\n", run ? "the game returned" : "check", result);
    return result;
}
