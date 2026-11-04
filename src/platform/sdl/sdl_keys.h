#pragma once

// SDL's keys as Windows' window procedure named them (the SDL backend, platform_sdl.cpp; tools/input_check compares
// them with Windows' own): the key (platform::Key, whose values are the virtual-key codes) and the set-1 scan code of a
// key, and the control character TranslateMessage made of a key press. Only SDL's types: no SDL call, no window.

#include "platform/platform.h"

#include <SDL3/SDL_keyboard.h>

namespace platform::sdl_keys {

// A key as Windows named it: the key and the scan code (set 1, 0x100 for an extended key).
struct KeyCode {
    Key key;
    int scan;
};

// The active keyboard layout's answers for SDL's keys, each system's (win32/keyboard.cpp: Windows' layouts;
// posix/keyboard.cpp: SDL's keymap read as Windows' layouts give theirs). LayoutKey: the key the layout makes of the
// one with the set-1 scan code `scan` (bit 8 for an extended key), as the window procedure's key messages carried it;
// Key::None when the system cannot say (KeyOf then names the key itself). LayoutControlCharacter: the control
// character the layout types for the key (`key`, scan code `scan`) with Ctrl held, as TranslateMessage made it: 0 for
// none; -1 when the system cannot say (ControlCharacter then applies its own rule).
Key LayoutKey(int scan);
int LayoutControlCharacter(Key key, int scan);

// The key the active keyboard layout makes of the one at a set-1 scan code, or Key::None (LayoutKey).
using LayoutKeyFunction = Key (*)(int scan);

// The key of `scancode` (its place on the keyboard); {Key::None, 0} for a key the game has no code for. The keys a layout
// moves (LayoutMoves) take the layout's key from `layoutKey` (the active layout's, as the window procedure got it on
// Windows); without one (Key::None or null) they and the other keys give Windows' US layout's. The number pad gives its
// digits with Num Lock on, else the keys printed below them.
KeyCode KeyOf(SDL_Scancode scancode, SDL_Keymod mod, LayoutKeyFunction layoutKey);

// Whether layouts move the key with the set-1 scan code `scan`: the digit row, the letters and the main block's
// punctuation.
bool LayoutMoves(int scan);

// The SDL scancode of the key with the set-1 scan code `scan` (KeyOf turned round), or SDL_SCANCODE_UNKNOWN.
SDL_Scancode ScancodeOf(int scan);

// The key a key is from what it types (`typed`, an SDL key code), where the system has no Windows layouts to say
// (posix/keyboard.cpp's LayoutKey): a Latin letter's or a digit's, and for the characters of Windows' US punctuation
// keys those keys (`-` Key::Minus, `;` Key::Semicolon, ...); Key::None for any other.
Key KeyOfTyped(SDL_Keycode typed);

// The control character the active layout types with Ctrl held for a key (the key, its set-1 scan code): 0 for none,
// -1 when the system cannot say (LayoutControlCharacter).
using LayoutControlFunction = int (*)(Key key, int scan);

// The control character TranslateMessage made of a key press (a WM_CHAR), or 0. Alt without Ctrl made none for the
// game (WM_SYSCHAR); SDL's text has no control characters. With Ctrl the layout's own when `layoutControl` says
// (Ctrl+[ is Escape's on a US layout, a letter's key on a Russian one), else Ctrl+letter 1..26, Ctrl+Escape 0x1B,
// Ctrl+Backspace 0x7F, Ctrl+Enter 0x0A.
int ControlCharacter(Key key, int scan, SDL_Keymod mod, LayoutControlFunction layoutControl);

} // namespace platform::sdl_keys
