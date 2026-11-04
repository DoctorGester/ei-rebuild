// SDL's keys as Windows' window procedure named them. See sdl_keys.h.
#include "platform/sdl/sdl_keys.h"

namespace platform::sdl_keys {

namespace {

// The key as Windows' US layout names it.
KeyCode UsKeyOf(SDL_Scancode scancode, SDL_Keymod mod) {
    static constexpr int kLetterScans[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                             0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
        const int index = scancode - SDL_SCANCODE_A;
        return {KeyAfter(Key::A, index), kLetterScans[index]};
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0) {
        const int index = scancode - SDL_SCANCODE_1;   // 1..9, then 0
        return {index == 9 ? Key::Digit0 : KeyAfter(Key::Digit0, index + 1), 0x02 + index};
    }
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F10)
        return {KeyAfter(Key::F1, scancode - SDL_SCANCODE_F1), 0x3B + (scancode - SDL_SCANCODE_F1)};
    const bool numbers = (mod & SDL_KMOD_NUM) != 0;
    switch (scancode) {
    case SDL_SCANCODE_RETURN: return {Key::Return, 0x1C};
    case SDL_SCANCODE_ESCAPE: return {Key::Escape, 0x01};
    case SDL_SCANCODE_BACKSPACE: return {Key::Backspace, 0x0E};
    case SDL_SCANCODE_TAB: return {Key::Tab, 0x0F};
    case SDL_SCANCODE_SPACE: return {Key::Space, 0x39};
    case SDL_SCANCODE_MINUS: return {Key::Minus, 0x0C};
    case SDL_SCANCODE_EQUALS: return {Key::Equals, 0x0D};
    case SDL_SCANCODE_LEFTBRACKET: return {Key::LeftBracket, 0x1A};
    case SDL_SCANCODE_RIGHTBRACKET: return {Key::RightBracket, 0x1B};
    case SDL_SCANCODE_BACKSLASH: return {Key::Backslash, 0x2B};
    case SDL_SCANCODE_NONUSHASH: return {Key::Backslash, 0x2B};
    case SDL_SCANCODE_SEMICOLON: return {Key::Semicolon, 0x27};
    case SDL_SCANCODE_APOSTROPHE: return {Key::Apostrophe, 0x28};
    case SDL_SCANCODE_GRAVE: return {Key::Grave, 0x29};
    case SDL_SCANCODE_COMMA: return {Key::Comma, 0x33};
    case SDL_SCANCODE_PERIOD: return {Key::Period, 0x34};
    case SDL_SCANCODE_SLASH: return {Key::Slash, 0x35};
    case SDL_SCANCODE_NONUSBACKSLASH: return {Key::NonUsBackslash, 0x56};
    case SDL_SCANCODE_CAPSLOCK: return {Key::CapsLock, 0x3A};
    case SDL_SCANCODE_F11: return {KeyAfter(Key::F1, 10), 0x57};
    case SDL_SCANCODE_F12: return {Key::F12, 0x58};
    case SDL_SCANCODE_PRINTSCREEN: return {Key::PrintScreen, 0x137};
    case SDL_SCANCODE_SCROLLLOCK: return {Key::ScrollLock, 0x46};
    case SDL_SCANCODE_PAUSE: return {Key::Pause, 0x45};
    case SDL_SCANCODE_INSERT: return {Key::Insert, 0x152};
    case SDL_SCANCODE_HOME: return {Key::Home, 0x147};
    case SDL_SCANCODE_PAGEUP: return {Key::PageUp, 0x149};
    case SDL_SCANCODE_DELETE: return {Key::Delete, 0x153};
    case SDL_SCANCODE_END: return {Key::End, 0x14F};
    case SDL_SCANCODE_PAGEDOWN: return {Key::PageDown, 0x151};
    case SDL_SCANCODE_RIGHT: return {Key::Right, 0x14D};
    case SDL_SCANCODE_LEFT: return {Key::Left, 0x14B};
    case SDL_SCANCODE_DOWN: return {Key::Down, 0x150};
    case SDL_SCANCODE_UP: return {Key::Up, 0x148};
    case SDL_SCANCODE_NUMLOCKCLEAR: return {Key::NumLock, 0x145};
    case SDL_SCANCODE_KP_DIVIDE: return {Key::KeypadDivide, 0x135};
    case SDL_SCANCODE_KP_MULTIPLY: return {Key::KeypadMultiply, 0x37};
    case SDL_SCANCODE_KP_MINUS: return {Key::KeypadMinus, 0x4A};
    case SDL_SCANCODE_KP_PLUS: return {Key::KeypadPlus, 0x4E};
    case SDL_SCANCODE_KP_ENTER: return {Key::Return, 0x11C};
    case SDL_SCANCODE_KP_1: return {numbers ? KeyAfter(Key::Keypad0, 1) : Key::End, 0x4F};
    case SDL_SCANCODE_KP_2: return {numbers ? KeyAfter(Key::Keypad0, 2) : Key::Down, 0x50};
    case SDL_SCANCODE_KP_3: return {numbers ? KeyAfter(Key::Keypad0, 3) : Key::PageDown, 0x51};
    case SDL_SCANCODE_KP_4: return {numbers ? KeyAfter(Key::Keypad0, 4) : Key::Left, 0x4B};
    case SDL_SCANCODE_KP_5: return {numbers ? KeyAfter(Key::Keypad0, 5) : Key::Clear, 0x4C};
    case SDL_SCANCODE_KP_6: return {numbers ? KeyAfter(Key::Keypad0, 6) : Key::Right, 0x4D};
    case SDL_SCANCODE_KP_7: return {numbers ? KeyAfter(Key::Keypad0, 7) : Key::Home, 0x47};
    case SDL_SCANCODE_KP_8: return {numbers ? KeyAfter(Key::Keypad0, 8) : Key::Up, 0x48};
    case SDL_SCANCODE_KP_9: return {numbers ? KeyAfter(Key::Keypad0, 9) : Key::PageUp, 0x49};
    case SDL_SCANCODE_KP_0: return {numbers ? Key::Keypad0 : Key::Insert, 0x52};
    case SDL_SCANCODE_KP_PERIOD: return {numbers ? Key::KeypadPeriod : Key::Delete, 0x53};
    case SDL_SCANCODE_APPLICATION: return {Key::Application, 0x15D};
    case SDL_SCANCODE_LCTRL: return {Key::Control, 0x1D};
    case SDL_SCANCODE_RCTRL: return {Key::Control, 0x11D};
    case SDL_SCANCODE_LSHIFT: return {Key::Shift, 0x2A};
    case SDL_SCANCODE_RSHIFT: return {Key::Shift, 0x36};
    case SDL_SCANCODE_LALT: return {Key::Alt, 0x38};
    case SDL_SCANCODE_RALT: return {Key::Alt, 0x138};
    case SDL_SCANCODE_LGUI: return {Key::LeftWindows, 0x15B};
    case SDL_SCANCODE_RGUI: return {Key::RightWindows, 0x15C};
    default: return {Key::None, 0};
    }
}

} // namespace

KeyCode KeyOf(SDL_Scancode scancode, SDL_Keymod mod, LayoutKeyFunction layoutKey) {
    KeyCode code = UsKeyOf(scancode, mod);
    if (code.key != Key::None && layoutKey && LayoutMoves(code.scan))
        if (const Key key = layoutKey(code.scan); key != Key::None) code.key = key;
    return code;
}

bool LayoutMoves(int scan) {
    return (scan >= 0x02 && scan <= 0x0D) || (scan >= 0x10 && scan <= 0x1B) || (scan >= 0x1E && scan <= 0x29) ||
           (scan >= 0x2B && scan <= 0x35) || scan == 0x56;
}

// Where two scancodes give one scan code (Backslash and the non-US hash key), the lower.
SDL_Scancode ScancodeOf(int scan) {
    struct Table {
        SDL_Scancode of[0x200];
    };
    static const Table table = [] {
        Table t{};
        for (int sc = SDL_SCANCODE_COUNT - 1; sc > SDL_SCANCODE_UNKNOWN; --sc)
            if (const KeyCode code = UsKeyOf(static_cast<SDL_Scancode>(sc), SDL_KMOD_NONE); code.key != Key::None)
                t.of[code.scan] = static_cast<SDL_Scancode>(sc);
        return t;
    }();
    return scan > 0 && scan < 0x200 ? table.of[scan] : SDL_SCANCODE_UNKNOWN;
}

Key KeyOfTyped(SDL_Keycode typed) {
    if (typed >= 'a' && typed <= 'z') return KeyAfter(Key::A, static_cast<int>(typed - 'a'));
    if (typed >= '0' && typed <= '9') return KeyAfter(Key::Digit0, static_cast<int>(typed - '0'));
    switch (typed) {
    case '-': return Key::Minus;
    case '=': return Key::Equals;
    case '[': return Key::LeftBracket;
    case ']': return Key::RightBracket;
    case '\\': return Key::Backslash;
    case ';': return Key::Semicolon;
    case '\'': return Key::Apostrophe;
    case '`': return Key::Grave;
    case ',': return Key::Comma;
    case '.': return Key::Period;
    case '/': return Key::Slash;
    default: return Key::None;
    }
}

int ControlCharacter(Key key, int scan, SDL_Keymod mod, LayoutControlFunction layoutControl) {
    const bool control = (mod & SDL_KMOD_CTRL) != 0;
    const bool alt = (mod & SDL_KMOD_ALT) != 0;
    if (alt) return 0;
    if (control) {
        if (layoutControl)
            if (const int character = layoutControl(key, scan); character >= 0) return character;
        if (key >= Key::A && key <= Key::Z) return static_cast<int>(key) - static_cast<int>(Key::A) + 1;
        if (key == Key::Escape) return 0x1B;
        if (key == Key::Backspace) return 0x7F;
        if (key == Key::Return) return 0x0A;
        return 0;
    }
    switch (key) {
    case Key::Backspace: return 0x08;
    case Key::Tab: return 0x09;
    case Key::Return: return 0x0D;
    case Key::Escape: return 0x1B;
    default: return 0;
    }
}

} // namespace platform::sdl_keys
