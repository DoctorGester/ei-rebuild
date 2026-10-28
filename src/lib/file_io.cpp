#include "file_io.h"

#include "gameapi.h"

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <io.h>

namespace lib {

std::FILE* OpenFile(const char* path, const char* mode) {
    const char kind = mode[0];
    if (kind != 'r' && kind != 'w' && kind != 'a') return nullptr;
    const bool update = std::strchr(mode + 1, '+') != nullptr;
    const bool binary = std::strchr(mode + 1, 'b') != nullptr;
    const DWORD access = update ? GENERIC_READ | GENERIC_WRITE : kind == 'r' ? GENERIC_READ : GENERIC_WRITE;
    const DWORD disposition = kind == 'r' ? OPEN_EXISTING : kind == 'w' ? CREATE_ALWAYS : OPEN_ALWAYS;
    const HANDLE handle = gameapi::CreateFileA(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, disposition,
                                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    // The descriptor takes the translation mode and the append flag, the stream the direction.
    const int fd = _open_osfhandle(reinterpret_cast<std::intptr_t>(handle), (binary ? 0 : _O_TEXT) | (kind == 'a' ? _O_APPEND : 0));
    if (fd == -1) {
        CloseHandle(handle);
        return nullptr;
    }
    const char streamMode[3] = {kind, update ? '+' : '\0', '\0'};
    std::FILE* file = _fdopen(fd, streamMode);
    if (!file) _close(fd);
    return file;
}

bool FileExists(const char* path) { return gameapi::GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

} // namespace lib
