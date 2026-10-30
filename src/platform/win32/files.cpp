// Folders and drives on Windows (platform/files.h). Folder creation and the volume label go through game.exe's import
// slots (the harness redirects folder creation; the image routes both to the UTF-8 calls); the free space was never a
// slot (the original looked GetDiskFreeSpaceExA up by name) and calls the system's wide function.
#include "platform/files.h"

#include "lib/gameapi.h"
#include "platform/win32/utf16.h"

#include <cstdint>
#include <windows.h>

namespace platform {

bool CreateFolder(const char* path) { return gameapi::CreateDirectoryA(path, nullptr) != FALSE; }

double FreeDiskSpace(const char* path) {
    ULARGE_INTEGER available;
    if (!GetDiskFreeSpaceExW(win32::Utf16(path).c_str(), &available, nullptr, nullptr)) return 0.0;
    return static_cast<double>(static_cast<std::int64_t>(available.QuadPart));
}

std::string VolumeLabel(const char* root) {
    char label[MAX_PATH + 1] = {};
    if (!gameapi::GetVolumeInformationA(root, label, sizeof(label), nullptr, nullptr, nullptr, nullptr, 0)) return {};
    return label;
}

} // namespace platform
