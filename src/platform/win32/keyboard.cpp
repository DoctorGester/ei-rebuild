// The platform's keyboard on Windows (platform.h, "The keyboard"): the keys' names in the active layout's language, the
// layout's codes and control characters for SDL's keys, the layout's choice and what a letter is.
#include "platform/platform.h"

#include "platform/win32/platform_win32.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace platform {

// The name in UTF-8, cut at a character's start to fit `size` bytes with the terminator.
bool KeyName(int scan, char* name, int size) {
    if (!name || size <= 0) return false;
    wchar_t wide[64] = {};
    const int length = GetKeyNameTextW(scan << 16, wide, 64);
    const std::string utf8 = text::Narrow(std::wstring_view(wide, static_cast<std::size_t>(length > 0 ? length : 0)));
    std::size_t n = utf8.size();
    if (n >= static_cast<std::size_t>(size)) {
        n = static_cast<std::size_t>(size) - 1;
        while (n > 0 && (static_cast<unsigned char>(utf8[n]) & 0xC0) == 0x80) --n;
    }
    std::memcpy(name, utf8.data(), n);
    name[n] = '\0';
    return n != 0;
}

// What Windows' layout maps the scan code to (the key messages' code).
int LayoutKey(int scan) {
    const UINT code = static_cast<UINT>(scan & 0xFF) | ((scan & 0x100) ? 0xE000u : 0u);
    return static_cast<int>(MapVirtualKeyExW(code, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0)));
}

// As LayoutKey: what the layout types with Ctrl held (flag 4: a dead key's state left as it was); only a
// control character counts.
int LayoutControlCharacter(int key, int scan) {
    BYTE state[256] = {};
    state[VK_CONTROL] = 0x80;
    wchar_t out[8] = {};
    const int n = ToUnicodeEx(static_cast<UINT>(key), static_cast<UINT>(scan & 0xFF), state, out, 8, 4, GetKeyboardLayout(0));
    return n == 1 && (out[0] < 0x20 || out[0] == 0x7F) ? out[0] : 0;
}

bool IsLetter(std::uint32_t character) { return character <= 0xFFFF && IsCharAlphaW(static_cast<WCHAR>(character)); }

void ActivateKeyboardLayout(std::uint32_t language, std::uint32_t fallback) {
    constexpr int kMaxLayouts = 16;
    HKL layouts[kMaxLayouts];
    const int count = ::GetKeyboardLayoutList(kMaxLayouts, layouts);
    int fallbackIndex = -1;
    int found = -1;
    HKL chosen = ::GetKeyboardLayout(0);
    for (int i = 0; i < count; ++i) {
        ::ActivateKeyboardLayout(layouts[i], 0);
        char name[KL_NAMELENGTH + 3];
        if (!::GetKeyboardLayoutNameA(name)) continue;
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
    ::ActivateKeyboardLayout(chosen, 0);
}

} // namespace platform
