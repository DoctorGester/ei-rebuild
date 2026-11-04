// The platform's paths on Linux and macOS (platform.h, "Paths"): the program's file, the current folder, the user's
// data folder (SDL's pref path) and the arguments, in the game's form.
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
#include <mach-o/dyld.h>
#endif

namespace platform {

namespace fs = std::filesystem;

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

std::string UserDataFolder() {
    char* path = SDL_GetPrefPath("", "");
    if (!path) return {};
    const std::string result = posix::GamePath(path) + "\\";
    SDL_free(path);
    return result;
}

bool SetCurrentFolder(const std::string& path, std::uint32_t* error) {
    if (chdir(NativePath(path).c_str()) == 0) return true;
    if (error) *error = errno;
    return false;
}

} // namespace platform
