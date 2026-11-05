// The platform's paths on Linux and macOS (platform.h, "Paths"): the program's file, the current folder, the user's data
// folder (SDL's pref path), the game's and the output's folders and the arguments, in the game's form. On macOS the
// program is inside ei-rebuild.app, so the game's folder is found or chosen (DefaultGameFolder) and the log goes to
// ~/Library/Logs/ei-rebuild.
#include "platform/platform.h"

#include "platform/posix/platform_posix.h"
#include "platform/seams.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <unistd.h>
#if defined(__APPLE__)
#include <atomic>
#include <fstream>
#include <mach-o/dyld.h>
#endif

namespace platform {

namespace fs = std::filesystem;

namespace {

// SDL's per-user folder of the application `app` ("" for the folder of all; made when missing), in the game's form with
// a trailing separator; empty when there is none.
std::string PrefFolder(const char* app) {
    char* path = SDL_GetPrefPath("", app);
    if (!path) return {};
    const std::string result = posix::GamePath(path) + "\\";
    SDL_free(path);
    return result;
}

#if defined(__APPLE__)

// Whether `folder` holds the game's data: its Res/database.res, each name matched with Windows' case rules as the game
// reads it.
bool HoldsGameData(const fs::path& folder) {
    return !folder.empty() && IsFile(posix::GamePath(folder / "Res" / "database.res"));
}

// The file that remembers the chosen game folder: ~/Library/Application Support/ei-rebuild/game_folder.txt, its folder
// made by SDL. Empty when there is no such folder.
fs::path RememberedFolderFile() {
    char* folder = SDL_GetPrefPath("", "ei-rebuild");
    if (!folder) return {};
    fs::path file = fs::path(folder) / "game_folder.txt";
    SDL_free(folder);
    return file;
}

// The folder picker's answer. SDL may answer on another thread, so `answered` is atomic.
struct FolderChoice {
    std::string folder;   // empty when the player cancelled
    std::atomic<bool> answered = false;
};

void SDLCALL TakeFolderChoice(void* userdata, const char* const* files, int) {
    auto* choice = static_cast<FolderChoice*>(userdata);
    if (files && files[0]) choice->folder = files[0];
    choice->answered = true;
}

// Asks the player for the game's folder until the one chosen holds the game's data; empty when the player cancels.
// SDL's video runs only while the picker does (Cocoa's application under it), so the window system starts afresh
// later (StartWindowSystem).
std::string ChooseGameFolder() {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return {};
    std::string folder;
    for (;;) {
        FolderChoice choice;
        const SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, "Choose the Evil Islands folder");
        SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_ACCEPT_STRING, "Play");
        SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER, &TakeFolderChoice, &choice, properties);
        while (!choice.answered) SDL_WaitEventTimeout(nullptr, 10);
        SDL_DestroyProperties(properties);
        if (choice.folder.empty() || HoldsGameData(choice.folder)) {
            folder = choice.folder;
            break;
        }
        const std::string text = choice.folder +
                                 " holds no Evil Islands data (Res/database.res). Choose the folder of an installed "
                                 "Evil Islands 1.06 (GOG or Rebirth), copied from Windows.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Evil Islands", text.c_str(), nullptr);
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return folder;
}

#else

// The program's file's folder, without a trailing separator.
std::string ProgramFolder() {
    std::string folder = ProgramPath();
    const std::size_t slash = folder.find_last_of("\\/");
    folder.resize(slash == std::string::npos ? 0 : slash);
    return folder;
}

#endif

} // namespace

std::string posix::GamePath(const fs::path& path) {
    std::string text = path.string();
    std::replace(text.begin(), text.end(), '/', '\\');
    return text;
}

std::string ProgramPath() {
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) return {};
    path.resize(std::strlen(path.c_str()));
    std::error_code error;
    return fs::canonical(path, error).string();
#else
    std::string path(PATH_MAX, '\0');
    const ssize_t size = readlink("/proc/self/exe", path.data(), path.size());
    return size > 0 ? path.substr(0, static_cast<std::size_t>(size)) : std::string{};
#endif
}

#if defined(__APPLE__)

// The program is <folder>/ei-rebuild.app/Contents/MacOS/ei-rebuild. A quarantined app runs translocated (from a
// random read-only copy), where nothing holds the game's data beside it: the picker then, its choice remembered.
std::string DefaultGameFolder() {
    const fs::path beside = fs::path(ProgramPath()).parent_path().parent_path().parent_path().parent_path();
    if (HoldsGameData(beside)) return beside.string();
    const fs::path remembered = RememberedFolderFile();
    std::string folder;
    if (std::ifstream file(remembered); file) std::getline(file, folder);
    if (HoldsGameData(folder)) return folder;
    folder = ChooseGameFolder();
    if (!folder.empty()) {
        std::ofstream file(remembered);
        file << folder << '\n';
    }
    return folder;
}

std::string DefaultOutputFolder() { return EnvironmentVariable("HOME") + "/Library/Logs/ei-rebuild"; }

std::string DefaultSettingsFolder() { return PrefFolder("ei-rebuild"); }

#else

std::string DefaultGameFolder() { return ProgramFolder(); }

std::string DefaultOutputFolder() { return ProgramFolder(); }

std::string DefaultSettingsFolder() {
    const std::string folder = ProgramFolder();
    return folder.empty() ? folder : folder + "/";
}

#endif

std::string ArgumentsAfterSeparator(int argc, char** argv) {
    std::string arguments;
    int i = 1;
    while (i < argc && std::strcmp(argv[i], "--") != 0) ++i;
    for (++i; i < argc; ++i) {
        if (!arguments.empty()) arguments += ' ';
        arguments += argv[i];
    }
    return arguments;
}

std::string CurrentFolder() { return posix::GamePath(fs::current_path()); }

std::string UserDataFolder() { return PrefFolder(""); }

bool SetCurrentFolder(const std::string& path, std::uint32_t* error) {
    if (chdir(NativePath(path).c_str()) == 0) return true;
    if (error) *error = errno;
    return false;
}

} // namespace platform
