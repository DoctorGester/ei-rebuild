// Check of the POSIX platform's keyboard on X11's layouts (src/platform/posix/platform_posix.cpp: LayoutKey,
// LayoutControlCharacter, KeyName through SDL's keymap): on an Xvfb of its own (never the session's
// display), setxkbmap's us, ru, fr and de in turn, every key a layout moves against the codes, names and Ctrl characters
// written below, as the SDL backend makes its events of them (sdl_keys::KeyOf, ControlCharacter). `--print` prints
// what each layout gives instead. Exit code 1 on a difference, 77 (skipped) without Xvfb or setxkbmap.
#include "platform/platform.h"
#include "platform/sdl/sdl_keys.h"

#include <SDL3/SDL.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {

using platform::sdl_keys::KeyCode;

// The moved keys' set-1 scan codes in order (sdl_keys::LayoutMoves).
std::vector<int> MovedScans() {
    std::vector<int> scans;
    for (int scan = 1; scan < 0x100; ++scan)
        if (platform::sdl_keys::LayoutMoves(scan)) scans.push_back(scan);
    return scans;
}

// What a layout gives its 48 moved keys: the virtual-key codes, the names (one word each), and the keys besides the
// letters that type a control character with Ctrl ("scan:character", hexadecimal).
struct Layout {
    const char* name;
    int keys[48];
    const char* names;
    const char* control;
};

// us and ru against Windows' own US and Russian layouts (MapVirtualKeyExW, GetKeyNameTextW, ToUnicodeEx): the same but
// for the key left of Z (scan 56), which xkb's us gives `<` and its ru `/` (Windows `\`). fr and de by LayoutKey's rule
// (what the key types): Windows' own French and German layouts give their punctuation keys other VK_OEM codes, and
// German's ß and - keys share VK_OEM_MINUS here. A dead key (French ^, German ´ and ^) types no character in SDL's
// keymap, so it has the US key's name.
const Layout kLayouts[] = {
    {"us",
     {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0xBD, 0xBB, 0x51, 0x57, 0x45, 0x52,
      0x54, 0x59, 0x55, 0x49, 0x4F, 0x50, 0xDB, 0xDD, 0x41, 0x53, 0x44, 0x46, 0x47, 0x48, 0x4A, 0x4B,
      0x4C, 0xBA, 0xDE, 0xC0, 0xDC, 0x5A, 0x58, 0x43, 0x56, 0x42, 0x4E, 0x4D, 0xBC, 0xBE, 0xBF, 0xE2},
     "1 2 3 4 5 6 7 8 9 0 - = Q W E R T Y U I O P [ ] A S D F G H J K L ; ' ` \\ Z X C V B N M , . / <",
     "1A:1B 1B:1D 2B:1C"},
    {"ru",
     {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0xBD, 0xBB, 0x51, 0x57, 0x45, 0x52,
      0x54, 0x59, 0x55, 0x49, 0x4F, 0x50, 0xDB, 0xDD, 0x41, 0x53, 0x44, 0x46, 0x47, 0x48, 0x4A, 0x4B,
      0x4C, 0xBA, 0xDE, 0xC0, 0xDC, 0x5A, 0x58, 0x43, 0x56, 0x42, 0x4E, 0x4D, 0xBC, 0xBE, 0xBF, 0xE2},
     "1 2 3 4 5 6 7 8 9 0 - = Q W E R T Y U I O P х ъ A S D F G H J K L ж э ё \\ Z X C V B N M б ю . /",
     "2B:1C"},
    {"fr",
     {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0xBD, 0xBB, 0x41, 0x5A, 0x45, 0x52,
      0x54, 0x59, 0x55, 0x49, 0x4F, 0x50, 0xDB, 0xDD, 0x51, 0x53, 0x44, 0x46, 0x47, 0x48, 0x4A, 0x4B,
      0x4C, 0x4D, 0xDE, 0xC0, 0xDC, 0x57, 0x58, 0x43, 0x56, 0x42, 0x4E, 0xBC, 0xBA, 0xBE, 0xBF, 0xE2},
     "& é \" ' ( - è _ ç à ) = A Z E R T Y U I O P [ $ Q S D F G H J K L M ù ² * W X C V B N , ; : ! <",
     ""},
    {"de",
     {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0xBD, 0xBB, 0x51, 0x57, 0x45, 0x52,
      0x54, 0x5A, 0x55, 0x49, 0x4F, 0x50, 0xDB, 0xDD, 0x41, 0x53, 0x44, 0x46, 0x47, 0x48, 0x4A, 0x4B,
      0x4C, 0xBA, 0xDE, 0xC0, 0xDC, 0x59, 0x58, 0x43, 0x56, 0x42, 0x4E, 0x4D, 0xBC, 0xBE, 0xBD, 0xE2},
     "1 2 3 4 5 6 7 8 9 0 ß = Q W E R T Z U I O P ü + A S D F G H J K L ö ä ` # Y X C V B N M , . - <",
     ""},
};

// The names of keys no layout moves (Windows' US names), on any layout.
struct FixedName {
    int scan;
    const char* name;
};
const FixedName kFixedNames[] = {{0x01, "Esc"},      {0x0E, "Backspace"}, {0x1D, "Ctrl"},       {0x39, "Space"},
                                 {0x45, "Pause"},    {0x47, "Num 7"},     {0x57, "F11"},        {0x11D, "Right Ctrl"},
                                 {0x145, "Num Lock"}, {0x147, "Home"},    {0x153, "Delete"},    {0x15B, "Left Windows"}};

bool OnPath(const char* program) {
    const char* path = std::getenv("PATH");
    std::string folders = path ? path : "/usr/bin:/bin";
    std::size_t start = 0;
    while (start <= folders.size()) {
        const std::size_t end = folders.find(':', start);
        const std::string folder = folders.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!folder.empty() && access((folder + "/" + program).c_str(), X_OK) == 0) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

// Runs a program to its end, quietly; its exit code, or -1.
int Run(std::vector<std::string> args) {
    std::vector<char*> argv;
    for (std::string& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    const int error = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

pid_t g_server = 0;

// An Xvfb on a free display number (from 99 down, as run_all.py starts one: under WSL Xvfb's own choice fails); its
// number, or -1.
int StartServer() {
    for (int display = 99; display >= 50; --display) {
        int fds[2];
        if (pipe(fds) != 0) return -1;
        const std::string number = ":" + std::to_string(display);
        const std::string fd = std::to_string(fds[1]);
        std::vector<std::string> args = {"Xvfb", number, "-screen", "0", "640x480x24", "-nolisten", "tcp", "-displayfd", fd};
        std::vector<char*> argv;
        for (std::string& arg : args) argv.push_back(arg.data());
        argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addclose(&actions, fds[0]);
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
        pid_t pid = 0;
        const int error = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        close(fds[1]);
        if (error) {
            close(fds[0]);
            return -1;
        }
        char line[16] = {};
        std::size_t got = 0;
        while (got + 1 < sizeof line) {   // the number and a newline once it is up; nothing when it died
            const ssize_t n = read(fds[0], line + got, 1);
            if (n <= 0 || line[got] == '\n') break;
            ++got;
        }
        close(fds[0]);
        if (got > 0 && line[0] >= '0' && line[0] <= '9') {
            g_server = pid;
            return std::atoi(line);
        }
        int status = 0;
        waitpid(pid, &status, 0);
    }
    return -1;
}

void StopServer() {
    if (g_server <= 0) return;
    kill(g_server, SIGTERM);
    int status = 0;
    waitpid(g_server, &status, 0);
    g_server = 0;
}

// Sets the server's layout and waits for SDL's keymap to follow (its keymap event, then a moment for any after it).
bool SetLayout(const std::string& display, const char* layout) {
    if (Run({"setxkbmap", "-display", display, "-layout", layout}) != 0) return false;
    bool changed = false;
    const Uint64 start = SDL_GetTicks();
    Uint64 quietFrom = 0;
    while (SDL_GetTicks() - start < 5000) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_KEYMAP_CHANGED) {
                changed = true;
                quietFrom = SDL_GetTicks();
            }
        if (changed && SDL_GetTicks() - quietFrom > 300) return true;
        SDL_Delay(10);
    }
    return changed;
}

std::vector<std::string> Words(const char* text) {
    std::vector<std::string> words;
    std::string word;
    for (const char* c = text; *c; ++c) {
        if (*c == ' ') {
            words.push_back(word);
            word.clear();
        } else {
            word += *c;
        }
    }
    if (!word.empty()) words.push_back(word);
    return words;
}

int g_problems = 0;
int g_checks = 0;

void Expect(bool same, const char* layout, int scan, const char* what, const std::string& ours, const std::string& want) {
    ++g_checks;
    if (same) return;
    if (++g_problems <= 40)
        std::printf("input_check: layout %s, scan %03X: %s %s, expected %s\n", layout, scan, what, ours.c_str(), want.c_str());
}

std::string Hex(int value) {
    char text[16];
    std::snprintf(text, sizeof text, "%02X", value);
    return text;
}

std::string Name(int scan, int size = 64) {
    char name[64] = {};
    return platform::KeyName(scan, name, size) ? name : "";
}

void Check(const Layout& layout, bool print) {
    const std::vector<int> scans = MovedScans();
    const std::vector<std::string> names = Words(layout.names);
    if (print) std::printf("%s:\n", layout.name);
    std::string keys, typed, control;
    for (std::size_t i = 0; i < scans.size(); ++i) {
        const int scan = scans[i];
        const KeyCode code = platform::sdl_keys::KeyOf(platform::sdl_keys::ScancodeOf(scan), SDL_KMOD_NONE, platform::LayoutKey);
        const int withCtrl = platform::sdl_keys::ControlCharacter(code.key, code.scan, SDL_KMOD_CTRL,
                                                                  platform::LayoutControlCharacter);
        const int plain = platform::sdl_keys::ControlCharacter(code.key, code.scan, SDL_KMOD_NONE,
                                                               platform::LayoutControlCharacter);
        const std::string name = Name(scan);
        const bool letter = code.key >= 'A' && code.key <= 'Z';
        if (print) {
            keys += "0x" + Hex(code.key) + (i + 1 < scans.size() ? ", " : "");
            typed += name + (i + 1 < scans.size() ? " " : "");
            if (withCtrl && !letter) control += (control.empty() ? "" : " ") + Hex(scan) + ":" + Hex(withCtrl);
            continue;
        }
        Expect(code.scan == scan, layout.name, scan, "scan", Hex(code.scan), Hex(scan));
        Expect(code.key == layout.keys[i], layout.name, scan, "key", Hex(code.key), Hex(layout.keys[i]));
        const std::string want = i < names.size() ? names[i] : "";
        Expect(name == want, layout.name, scan, "name", name, want);
        int wantCtrl = letter ? code.key - 'A' + 1 : 0;
        const std::string tag = Hex(scan) + ":";
        if (const char* at = std::strstr(layout.control, tag.c_str())) wantCtrl = static_cast<int>(std::strtol(at + 3, nullptr, 16));
        Expect(withCtrl == wantCtrl, layout.name, scan, "Ctrl character", Hex(withCtrl), Hex(wantCtrl));
        Expect(plain == 0, layout.name, scan, "character", Hex(plain), "00");
    }
    if (print) {
        std::printf("  keys: %s\n  names: %s\n  control: %s\n", keys.c_str(), typed.c_str(), control.c_str());
        return;
    }
    for (const FixedName& fixed : kFixedNames) Expect(Name(fixed.scan) == fixed.name, layout.name, fixed.scan, "name", Name(fixed.scan), fixed.name);
    // A name cut to its buffer at a character's start: one byte left for 2-byte "х" is none, for "Q" the letter.
    if (std::string(layout.name) == "ru") Expect(Name(0x1A, 2).empty(), layout.name, 0x1A, "name in 2 bytes", Name(0x1A, 2), "");
    Expect(Name(0x10, 2).size() == 1, layout.name, 0x10, "name in 2 bytes", Name(0x10, 2), "one letter");
}

} // namespace

int main(int argc, char** argv) {
    const bool print = argc > 1 && std::strcmp(argv[1], "--print") == 0;
    if (!OnPath("Xvfb") || !OnPath("setxkbmap")) {
        std::printf("input_check: skipped, no Xvfb or setxkbmap\n");
        return 77;
    }
    const int number = StartServer();
    if (number < 0) {
        std::printf("input_check: Xvfb did not start\n");
        return 1;
    }
    const std::string display = ":" + std::to_string(number);
    setenv("DISPLAY", display.c_str(), 1);
    unsetenv("WAYLAND_DISPLAY");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
    if (Run({"setxkbmap", "-display", display, "-layout", "us"}) != 0 || !SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("input_check: no X11 keyboard on %s: %s\n", display.c_str(), SDL_GetError());
        StopServer();
        return 1;
    }
    // us last, so that each layout is a change SDL sees.
    for (const char* name : {"ru", "fr", "de", "us"}) {
        if (!SetLayout(display, name)) {
            std::printf("input_check: layout %s did not reach SDL's keymap\n", name);
            ++g_problems;
            continue;
        }
        for (const Layout& layout : kLayouts)
            if (std::strcmp(layout.name, name) == 0) Check(layout, print);
    }
    SDL_Quit();
    StopServer();
    if (!print) std::printf("input_check: %d layouts, %d checks, %d problem(s)\n", 4, g_checks, g_problems);
    return g_problems ? 1 : 0;
}
