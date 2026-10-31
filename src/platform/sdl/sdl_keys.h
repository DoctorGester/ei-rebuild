#pragma once

// SDL's keys as Windows' window procedure named them (the SDL backend, platform_sdl.cpp; tools/input_check compares
// them with Windows' own): the virtual-key code and the set-1 scan code of a key, and the control character
// TranslateMessage made of a key press. Only SDL's types: no SDL call, no window.

#include <SDL3/SDL_keyboard.h>

namespace platform::sdl_keys {

// A key as Windows named it: the virtual-key code and the scan code (set 1, 0x100 for an extended key).
struct KeyCode {
    int key;
    int scan;
};

// The virtual-key code the active keyboard layout gives the key at a set-1 scan code, or 0 (platform::LayoutKey).
using LayoutKeyFunction = int (*)(int scan);

// The key of `scancode` (its place on the keyboard); {0, 0} for a key the game has no code for. The keys a layout
// moves (letters, the digit row, punctuation) take the layout's code from `layoutKey` (Windows: the active layout's,
// as the window procedure got it); without one (0 or null) a letter or digit key gives the code of what it types
// (SDL's key code, Latin letters on every layout: SDL_HINT_KEYCODE_OPTIONS) and the others Windows' US layout's. The
// number pad gives its digits with Num Lock on, else the keys printed below them.
KeyCode KeyOf(SDL_Scancode scancode, SDL_Keycode typed, SDL_Keymod mod, LayoutKeyFunction layoutKey);

// The control character the active layout types with Ctrl held for a key (code, set-1 scan code): 0 for none, -1 when
// the system cannot say (platform::LayoutControlCharacter).
using LayoutControlFunction = int (*)(int key, int scan);

// The control character TranslateMessage made of a key press (a WM_CHAR), or 0. Alt without Ctrl made none for the
// game (WM_SYSCHAR); SDL's text has no control characters. With Ctrl the layout's own when `layoutControl` says
// (Ctrl+[ is Escape's on a US layout, a letter's key on a Russian one), else Ctrl+letter 1..26, Ctrl+Escape 0x1B,
// Ctrl+Backspace 0x7F, Ctrl+Enter 0x0A.
int ControlCharacter(int key, int scan, SDL_Keymod mod, LayoutControlFunction layoutControl);

} // namespace platform::sdl_keys
