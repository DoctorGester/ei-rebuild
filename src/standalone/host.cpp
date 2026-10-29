// ei_standalone: the rebuilt game, without game.exe. See docs/notes/standalone.md, "The image's source files".
// usage: ei_standalone [--check] [-- <game arguments>]
//   The game folder is the exe's own (under ei_launch: the data folder it names, EI_GAME_ROOT). With --check the exe
//   builds the image, takes the registrations, checks them and exits: no window, nothing of the game runs. What
//   follows "--" is the game's command line, as typed (WinMain's).

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace ei {
int StandaloneMain(bool run, const char* arguments);   // standalone_main.cpp
}

namespace {

// The game's arguments: what follows the token "--" on the exe's command line, as typed.
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

// Marks this process as the standalone image (ei::Standalone): game.exe, where the same harness runs from
// ei_harness.dll, exports nothing.
extern "C" __declspec(dllexport) int EiStandaloneHost = 1;

int main(int argc, char** argv) {
    // Under ei_launch --standalone stdout is a pipe: unbuffered, so a run that ends on a missing-code report
    // (TerminateProcess) loses none of it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    bool run = true;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--check")) run = false;
        else if (!std::strcmp(argv[i], "--")) break;
        else {
            std::fprintf(stderr, "usage: ei_standalone [--check] [-- <game arguments>]\n");
            return 2;
        }
    }
    const int result = ei::StandaloneMain(run, GameArguments().c_str());
    std::printf("ei_standalone: %s %d\n", run ? "the game returned" : "check", result);
    return result;
}
