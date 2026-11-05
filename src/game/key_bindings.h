#pragma once

// The key bindings file, config\keyboard.ini.

// The in-game key bindings' size (g_keyActions).
constexpr int kKeyActionCount = 0x200;

namespace keybind {
void LoadKeyBindings();
void SaveKeyBindings();
}
