#pragma once

#include <cstdint>
#include <cstdio>

// The harness's input is the game's only input (harness_input.cpp).
//
// A harness game reads the keyboard and the mouse as the platform layer's events (keys, characters, the mouse's
// moves and buttons, the wheel), the mouse's position (CursorPosition: the cursor, edge scrolling) and the mouse's
// motion (MouseDevice: the camera on a right-button drag), all from the harness's own platform backend
// (platform/harness_platform.h): its queue of the harness's events, its motion count, its position from the harness's
// mouse. The real keyboard and mouse never reach the game: the backend drops every system event (S21c1: the real
// mouse made the original's runs stop repeating). The steps still speak the window messages the game's window
// procedure took (key down, left button down, ...; their numbers and words as Windows' were): Post makes each into
// the event the window procedure's translation made of it.

namespace ei::verify::input {

// The messages (Windows' WM_ numbers).
enum Message : unsigned {
    Activate = 0x0006,      // word: active (1, 2) or not (0)
    KeyDown = 0x0100,       // word: the key; data: the repeat count, scan code << 16, flags
    KeyUp = 0x0101,
    Character = 0x0102,     // word: the character
    SystemKeyDown = 0x0104,
    SystemKeyUp = 0x0105,
    MouseMove = 0x0200,     // word: the buttons and keys held (kLeftButton, ...); data: Point(x, y)
    LeftDown = 0x0201,
    LeftUp = 0x0202,
    LeftDouble = 0x0203,
    RightDown = 0x0204,
    RightUp = 0x0205,
    RightDouble = 0x0206,
    Wheel = 0x020A,         // word: the buttons, the delta << 16 (120 a notch)
};

// The buttons and keys a mouse message holds (Windows' MK_).
inline constexpr unsigned kLeftButton = 0x0001, kRightButton = 0x0002;

// A mouse message's point (client coordinates).
inline std::intptr_t Point(int x, int y) {
    return static_cast<std::intptr_t>((static_cast<std::uint32_t>(y & 0xFFFF) << 16) | static_cast<std::uint32_t>(x & 0xFFFF));
}
// A wheel message's word.
inline std::uintptr_t WheelWord(unsigned buttons, int delta) {
    return (static_cast<std::uint32_t>(delta & 0xFFFF) << 16) | (buttons & 0xFFFF);
}

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

// The window message `message` (word, data as the steps post them) as the game's event, queued for its loops;
// Activate tells the game at once (the focus steps, as SendMessage did).
void Post(unsigned message, std::uintptr_t word, std::intptr_t data);

// The harness's own motion (the "mousedrag" step): the game's next read of its mouse reports (dx, dy) more.
void AddMotion(int dx, int dy);

// The report's MOUSE: line.
void Report(FILE* f);

} // namespace ei::verify::input
