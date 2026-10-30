#pragma once

// A game path's parts as the C library's _splitpath gives them, on any system: the game's paths are Windows paths,
// whose folders a backslash or a slash ends.

#include <string>
#include <string_view>

namespace lib {

struct PathParts {
    std::string drive;       // "C:", or empty
    std::string directory;   // up to and with the last separator
    std::string name;
    std::string extension;   // from the name's last dot, with it
};

inline PathParts SplitPath(std::string_view path) {
    PathParts parts;
    if (path.size() >= 2 && path[1] == ':') {
        parts.drive = path.substr(0, 2);
        path.remove_prefix(2);
    }
    const std::size_t folderEnd = path.find_last_of("\\/") + 1;   // 0 without a separator
    parts.directory = path.substr(0, folderEnd);
    path.remove_prefix(folderEnd);
    const std::size_t dot = path.find_last_of('.');
    parts.name = path.substr(0, dot);
    if (dot != std::string_view::npos) parts.extension = path.substr(dot);
    return parts;
}

} // namespace lib
