// Checks for app_dxversion.cpp: ours only (it creates DirectDraw objects and loads libraries);
// the result must be one of the versions GetDXVersion reports.
#include "verify.h"

#include <cstdint>

namespace app {
std::uint32_t __cdecl GetDxVersion();
}

namespace {
void* s_unused;

std::uint32_t __cdecl DxVersionVerify() {
    std::uint32_t version = app::GetDxVersion();
    bool ok = false;
    for (std::uint32_t v : {0u, 0x100u, 0x200u, 0x300u, 0x500u, 0x600u, 0x601u, 0x700u, 0x800u}) ok = ok || v == version;
    ei::verify::Check("GetDxVersion", true, ok, "");
    return version;
}
} // namespace

EI_VERIFY(0x0045E310, DxVersionVerify, 0, s_unused);
