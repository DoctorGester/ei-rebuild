#pragma once

// The keyboard (docs/notes/platform.md, "Window and events"): the keys' names, the layout typed characters come from
// and what kind of character one is.

#include <cstdint>

namespace platform {

// The name the system gives the key with the scan code `scan` (bit 8 for an extended key) in the active layout's
// language, UTF-8 into `name` (`size` bytes with the terminator, cut at a character). False when it has none.
bool KeyName(int scan, char* name, int size);
// Activates the keyboard layout of `language` (a Windows language id), else one of `fallback`, else keeps the one in
// use. Each installed layout is active for a moment on the way (only the active one tells its language).
void ActivateKeyboardLayout(std::uint32_t language, std::uint32_t fallback);
// Whether the system calls the typed character `character` (a code point; false past U+FFFF) a letter.
bool IsLetter(std::uint32_t character);

} // namespace platform
