#pragma once

// What the machine is (docs/notes/platform.md).

#include <cstdint>

namespace platform {

// The size of a virtual memory page in bytes.
std::uint32_t PageSize();

} // namespace platform
