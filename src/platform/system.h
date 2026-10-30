#pragma once

// What the machine is (docs/notes/platform.md).

#include <cstdint>

namespace platform {

// The size of a virtual memory page in bytes.
std::uint32_t PageSize();
// Whether the desktop smooths the edges of text; true when it cannot tell.
bool FontSmoothing();

} // namespace platform
