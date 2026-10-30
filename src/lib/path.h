#pragma once

#include <algorithm>
#include <cstring>
#include <string_view>

namespace lib {

// Split Windows asset paths on either separator, even when running on a POSIX host.
template <std::size_t D, std::size_t P, std::size_t N, std::size_t E>
void SplitPath(std::string_view path, char (&drive)[D], char (&directory)[P], char (&name)[N], char (&ext)[E]) {
    const auto copy = [](auto& to, std::string_view from) {
        const std::size_t count = (std::min)(sizeof(to) - 1, from.size());
        std::memcpy(to, from.data(), count);
        to[count] = 0;
    };
    const bool hasDrive = path.size() >= 2 && path[1] == ':';
    copy(drive, path.substr(0, hasDrive ? 2 : 0));
    if (hasDrive) path.remove_prefix(2);
    const std::size_t slash = path.find_last_of("/\\");
    copy(directory, slash == path.npos ? std::string_view{} : path.substr(0, slash + 1));
    if (slash != path.npos) path.remove_prefix(slash + 1);
    const std::size_t dot = path.find_last_of('.');
    copy(name, path.substr(0, dot));
    copy(ext, dot == path.npos ? std::string_view{} : path.substr(dot));
}

}   // namespace lib
