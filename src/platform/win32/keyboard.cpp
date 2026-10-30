// The keyboard on Windows (platform/keyboard.h). Through game.exe's import slots; the image routes the key names to
// the UTF-8 call.
#include "platform/keyboard.h"

#include "lib/gameapi.h"

#include <cstdio>
#include <windows.h>

namespace platform {

bool KeyName(int scan, char* name, int size) { return gameapi::GetKeyNameTextA(scan << 16, name, size) != 0; }

void ActivateKeyboardLayout(std::uint32_t language, std::uint32_t fallback) {
    constexpr int kMaxLayouts = 16;
    HKL layouts[kMaxLayouts];
    const int count = gameapi::GetKeyboardLayoutList(kMaxLayouts, layouts);
    int fallbackIndex = -1;
    int found = -1;
    HKL chosen = gameapi::GetKeyboardLayout(0);
    for (int i = 0; i < count; ++i) {
        gameapi::ActivateKeyboardLayout(layouts[i], 0);
        char name[KL_NAMELENGTH + 3];
        if (!gameapi::GetKeyboardLayoutNameA(name)) continue;
        unsigned id = 0;
        sscanf_s(name, "%x", &id);
        id &= 0xFFFF;
        if (id == language) {
            found = i;
            break;
        }
        if (id == fallback) fallbackIndex = i;
    }
    if (found >= 0)
        chosen = layouts[found];
    else if (fallbackIndex >= 0)
        chosen = layouts[fallbackIndex];
    gameapi::ActivateKeyboardLayout(chosen, 0);
}

} // namespace platform
