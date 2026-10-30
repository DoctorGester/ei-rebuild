// What the machine is, on Windows (platform/system.h).
#include "platform/system.h"

#include "lib/gameapi.h"

#include <windows.h>

namespace platform {

std::uint32_t PageSize() {
    SYSTEM_INFO info;
    gameapi::GetSystemInfo(&info);
    return info.dwPageSize;
}

} // namespace platform
