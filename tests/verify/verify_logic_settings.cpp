// Checks for LogicSettings::Load (src/game/logic_settings.cpp): it writes only the settings. The
// settings are kept, the original reads the file, its result is kept and the first values put
// back, then ours reads it; the two results compared byte for byte.
#include "verify.h"

#include "../../src/game/logic_settings.h"

#include <cstring>

namespace {
using LoadFn = void(__fastcall*)(LogicSettings*, void*, const char*);
LoadFn s_originalLoad;
}

// 005CE0F0 (MOV EAX,FS:[0]: 6 bytes).
static void __fastcall LoadVerify(LogicSettings* self, void*, const char* path) {
    LogicSettings before, expected;
    std::memcpy(&before, self, sizeof(before));
    s_originalLoad(self, nullptr, path);
    std::memcpy(&expected, self, sizeof(expected));
    std::memcpy(self, &before, sizeof(before));
    self->Load(path);
    ei::verify::Check("LogicSettings::Load", true, std::memcmp(&expected, self, sizeof(expected)) == 0, "%s",
                      path ? path : "(null)");
}
EI_VERIFY(0x005CE0F0, LoadVerify, 6, s_originalLoad);
