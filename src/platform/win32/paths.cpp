// Where the program runs from, on Windows (platform/paths.h). Through game.exe's import slots, which the image routes
// to the UTF-8 calls.
#include "platform/paths.h"

#include "lib/gameapi.h"

#include <utility>

namespace platform {

namespace {

// Room for any path the calls return (the long-path limit, in UTF-8 bytes).
constexpr DWORD kPathBufferSize = 0x8000;

// `path` cut to the length a call returned: none when the call failed or the path did not fit.
std::string Fitted(std::string path, DWORD length) {
    path.resize(length < kPathBufferSize ? length : 0);
    return path;
}

} // namespace

std::string ExePath() {
    std::string path(kPathBufferSize, '\0');
    const DWORD length = gameapi::GetModuleFileNameA(nullptr, path.data(), kPathBufferSize);
    return Fitted(std::move(path), length);
}

std::string CurrentFolder() {
    std::string folder(kPathBufferSize, '\0');
    const DWORD length = gameapi::GetCurrentDirectoryA(kPathBufferSize, folder.data());
    return Fitted(std::move(folder), length);
}

} // namespace platform
