#pragma once

// The key bindings file, config\keyboard.ini, and its tables.

#include <cstdint>

// The in-game key bindings' size (g_keyActions).
constexpr int kKeyActionCount = 0x200;

namespace keybind {
void LoadKeyBindings();
void SaveKeyBindings();
}

// The in-game key bindings (config\keyboard.ini; key_bindings.cpp): the action of each scan code
// (0x200 entries, the WM_KEYDOWN flags & 0x1FF; the options window's OK stores its copy there).
inline std::int8_t g_keyActions[512] = /*0079BF70*/ {};

// The keys (0x48 bytes each, 88 of them; key_bindings.cpp's g_keyNames): the options window lists them, and
// the bindings file names them.
struct UiKeyName {
    int scan = 0;                    // scan code, the index into g_keyActions
    const char* file = nullptr;      // the bindings file's name ("TAB", "Q", ...)
    char name[0x40] = {};            // GetKeyNameTextA's text (up to 31 bytes; LoadKeyBindings)
};
constexpr int kUiKeyNameCount = 88;
extern UiKeyName g_keyNames[kUiKeyNameCount];

// The command bound to `key`: the text of the first key of g_keyNames whose scan code i (0..0x1FF)
// has `key` in g_keyActions, or null.
const char* FindCommandByKey(int key);
