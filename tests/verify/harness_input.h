#pragma once

#include "platform/platform.h"

#include <cstdint>
#include <cstdio>

// The harness's input is the game's only input (harness_input.cpp).
//
// A harness game reads the keyboard and the mouse as the platform layer's events (keys, characters, the mouse's
// moves and buttons, the wheel), the mouse's position (CursorPosition: the cursor, edge scrolling) and the mouse's
// motion (TakeMouseMotion: the camera on a right-button drag), all from the harness's own platform backend
// (platform/harness_platform.h): its queue of the harness's events, its motion count, its position from the harness's
// mouse. The real keyboard and mouse never reach the game: the backend drops every system event (S21c1: the real
// mouse made the original's runs stop repeating). The steps queue the platform's events for the game's loops.

namespace ei::verify::input {

// A key pressed or released: its virtual-key code (Key below), its set-1 scan code (0x100 extended) and the
// modifiers held (platform::kModifier...).
void PressKey(unsigned key, unsigned scan, std::uint8_t modifiers = 0);
void ReleaseKey(unsigned key, unsigned scan, std::uint8_t modifiers = 0);
// A typed character (at most U+FFFF) with the scan code of the key that typed it.
void TypeCharacter(unsigned character, unsigned scan);
// The mouse at `at` (the window's client pixels): moved there, a button pressed (`clicks` 2: a double click's second
// press) or released, the wheel turned (`turn` 120 a notch, positive away from the player).
void MoveMouse(Point at);
void PressButton(platform::MouseButton button, Point at, int clicks = 1);
void ReleaseButton(platform::MouseButton button, Point at);
void TurnWheel(int turn, Point at);

// The keys the steps press (Windows' virtual-key codes; a letter or digit is its upper case character).
enum Key : unsigned {
    kKeyBack = 0x08, kKeyTab = 0x09, kKeyReturn = 0x0D, kKeyShift = 0x10, kKeyControl = 0x11, kKeyMenu = 0x12,
    kKeyEscape = 0x1B, kKeySpace = 0x20, kKeyPageUp = 0x21, kKeyPageDown = 0x22, kKeyEnd = 0x23, kKeyHome = 0x24,
    kKeyLeft = 0x25, kKeyUp = 0x26, kKeyRight = 0x27, kKeyDown = 0x28, kKeyDelete = 0x2E, kKeyMultiply = 0x6A,
    kKeyAdd = 0x6B, kKeySubtract = 0x6D, kKeyDivide = 0x6F, kKeyF1 = 0x70, kKeyScroll = 0x91, kKeyOem1 = 0xBA,
    kKeyOemPlus = 0xBB, kKeyOemComma = 0xBC, kKeyOemMinus = 0xBD, kKeyOemPeriod = 0xBE, kKeyOem2 = 0xBF,
    kKeyOem3 = 0xC0, kKeyOem4 = 0xDB, kKeyOem5 = 0xDC, kKeyOem6 = 0xDD, kKeyOem7 = 0xDE,
};

// A key's scan code (set 1) on a US keyboard, as Windows' MapVirtualKey gave it; 0 for none.
unsigned ScanCode(unsigned key);

// The harness's own motion (the "mousedrag" step): the game's next read of its mouse reports (dx, dy) more.
void AddMotion(int dx, int dy);

// The report's MOUSE: line.
void Report(FILE* f);

} // namespace ei::verify::input
