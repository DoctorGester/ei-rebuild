// Checks for key_bindings.cpp: the original loads the bindings first; the bound keys (0x200
// bytes) and the commands' key texts are recorded; ours loads the same file again and both are
// compared.
#include "verify.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace keybind {
void __cdecl LoadKeyBindings();
}

namespace {
constexpr std::uintptr_t kBindings = 0x0079BF70;
constexpr std::uintptr_t kCommands = 0x007857B0;
constexpr std::size_t kCommandsSize = 0x00787070 - 0x007857B0;

// 00462C10: MOV EAX, FS:[0] (6).
void(__cdecl* s_load)();
void __cdecl LoadVerify() {
    s_load();
    std::uint8_t bindings[0x200];
    std::memcpy(bindings, reinterpret_cast<const void*>(kBindings), sizeof(bindings));
    std::string commands(reinterpret_cast<const char*>(kCommands), kCommandsSize);
    keybind::LoadKeyBindings();
    int bad = -1;
    for (int i = 0; bad < 0 && i < 0x200; ++i)
        if (bindings[i] != reinterpret_cast<const std::uint8_t*>(kBindings)[i]) bad = i;
    bool textsSame = std::memcmp(commands.data(), reinterpret_cast<const void*>(kCommands), kCommandsSize) == 0;
    ei::verify::Check("LoadKeyBindings", true, bad < 0 && textsSame, std::to_string(bad).c_str());
}
} // namespace

EI_VERIFY(0x00462C10, LoadVerify, 6, s_load);
