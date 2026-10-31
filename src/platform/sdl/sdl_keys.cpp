// SDL's keys as Windows' window procedure named them. See sdl_keys.h.
#include "platform/sdl/sdl_keys.h"

#include "platform/platform.h"

namespace platform::sdl_keys {

namespace {

// Without the layout's codes: a letter or digit key gives the code of what it types (SDL's key code, Latin letters on
// every layout), as Windows' layouts did; 0 for another.
int TypedKey(SDL_Keycode typed) {
    if (typed >= 'a' && typed <= 'z') return static_cast<int>(typed - 'a' + 'A');
    if (typed >= '0' && typed <= '9') return static_cast<int>(typed);
    return 0;
}

// The keys a layout moves, by their set-1 scan code: the digit row, the letters and the punctuation of the main block.
bool LayoutMoves(int scan) {
    return (scan >= 0x02 && scan <= 0x0D) || (scan >= 0x10 && scan <= 0x1B) || (scan >= 0x1E && scan <= 0x29) ||
           (scan >= 0x2B && scan <= 0x35) || scan == 0x56;
}

// The key as Windows' US layout names it (the letters and digits by what they type).
KeyCode UsKeyOf(SDL_Scancode scancode, SDL_Keycode typed, SDL_Keymod mod) {
    static constexpr int kLetterScans[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                             0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
        const int index = scancode - SDL_SCANCODE_A;
        const int key = TypedKey(typed);
        return {key ? key : 'A' + index, kLetterScans[index]};
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0) {
        const int index = scancode - SDL_SCANCODE_1;   // 1..9, then 0
        const int key = TypedKey(typed);
        return {key ? key : (index == 9 ? '0' : '1' + index), 0x02 + index};
    }
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F10)
        return {0x70 + (scancode - SDL_SCANCODE_F1), 0x3B + (scancode - SDL_SCANCODE_F1)};
    const bool numbers = (mod & SDL_KMOD_NUM) != 0;
    switch (scancode) {
    case SDL_SCANCODE_RETURN: return {kKeyReturn, 0x1C};
    case SDL_SCANCODE_ESCAPE: return {kKeyEscape, 0x01};
    case SDL_SCANCODE_BACKSPACE: return {kKeyBack, 0x0E};
    case SDL_SCANCODE_TAB: return {kKeyTab, 0x0F};
    case SDL_SCANCODE_SPACE: return {kKeySpace, 0x39};
    case SDL_SCANCODE_MINUS: return {0xBD, 0x0C};          // VK_OEM_MINUS
    case SDL_SCANCODE_EQUALS: return {0xBB, 0x0D};         // VK_OEM_PLUS
    case SDL_SCANCODE_LEFTBRACKET: return {0xDB, 0x1A};    // VK_OEM_4
    case SDL_SCANCODE_RIGHTBRACKET: return {0xDD, 0x1B};   // VK_OEM_6
    case SDL_SCANCODE_BACKSLASH: return {0xDC, 0x2B};      // VK_OEM_5
    case SDL_SCANCODE_NONUSHASH: return {0xDC, 0x2B};
    case SDL_SCANCODE_SEMICOLON: return {0xBA, 0x27};      // VK_OEM_1
    case SDL_SCANCODE_APOSTROPHE: return {0xDE, 0x28};     // VK_OEM_7
    case SDL_SCANCODE_GRAVE: return {0xC0, 0x29};          // VK_OEM_3: the key under Escape (the console's)
    case SDL_SCANCODE_COMMA: return {0xBC, 0x33};          // VK_OEM_COMMA
    case SDL_SCANCODE_PERIOD: return {0xBE, 0x34};         // VK_OEM_PERIOD
    case SDL_SCANCODE_SLASH: return {0xBF, 0x35};          // VK_OEM_2
    case SDL_SCANCODE_NONUSBACKSLASH: return {0xE2, 0x56}; // VK_OEM_102
    case SDL_SCANCODE_CAPSLOCK: return {0x14, 0x3A};
    case SDL_SCANCODE_F11: return {0x7A, 0x57};
    case SDL_SCANCODE_F12: return {0x7B, 0x58};
    case SDL_SCANCODE_PRINTSCREEN: return {0x2C, 0x137};
    case SDL_SCANCODE_SCROLLLOCK: return {0x91, 0x46};
    case SDL_SCANCODE_PAUSE: return {0x13, 0x45};
    case SDL_SCANCODE_INSERT: return {0x2D, 0x152};
    case SDL_SCANCODE_HOME: return {kKeyHome, 0x147};
    case SDL_SCANCODE_PAGEUP: return {kKeyPageUp, 0x149};
    case SDL_SCANCODE_DELETE: return {kKeyDelete, 0x153};
    case SDL_SCANCODE_END: return {kKeyEnd, 0x14F};
    case SDL_SCANCODE_PAGEDOWN: return {kKeyPageDown, 0x151};
    case SDL_SCANCODE_RIGHT: return {kKeyRight, 0x14D};
    case SDL_SCANCODE_LEFT: return {kKeyLeft, 0x14B};
    case SDL_SCANCODE_DOWN: return {kKeyDown, 0x150};
    case SDL_SCANCODE_UP: return {kKeyUp, 0x148};
    case SDL_SCANCODE_NUMLOCKCLEAR: return {0x90, 0x145};
    case SDL_SCANCODE_KP_DIVIDE: return {0x6F, 0x135};
    case SDL_SCANCODE_KP_MULTIPLY: return {0x6A, 0x37};
    case SDL_SCANCODE_KP_MINUS: return {0x6D, 0x4A};
    case SDL_SCANCODE_KP_PLUS: return {0x6B, 0x4E};
    case SDL_SCANCODE_KP_ENTER: return {kKeyReturn, 0x11C};
    case SDL_SCANCODE_KP_1: return {numbers ? 0x61 : kKeyEnd, 0x4F};
    case SDL_SCANCODE_KP_2: return {numbers ? 0x62 : kKeyDown, 0x50};
    case SDL_SCANCODE_KP_3: return {numbers ? 0x63 : kKeyPageDown, 0x51};
    case SDL_SCANCODE_KP_4: return {numbers ? 0x64 : kKeyLeft, 0x4B};
    case SDL_SCANCODE_KP_5: return {numbers ? 0x65 : 0x0C, 0x4C};   // VK_CLEAR
    case SDL_SCANCODE_KP_6: return {numbers ? 0x66 : kKeyRight, 0x4D};
    case SDL_SCANCODE_KP_7: return {numbers ? 0x67 : kKeyHome, 0x47};
    case SDL_SCANCODE_KP_8: return {numbers ? 0x68 : kKeyUp, 0x48};
    case SDL_SCANCODE_KP_9: return {numbers ? 0x69 : kKeyPageUp, 0x49};
    case SDL_SCANCODE_KP_0: return {numbers ? 0x60 : 0x2D, 0x52};
    case SDL_SCANCODE_KP_PERIOD: return {numbers ? 0x6E : kKeyDelete, 0x53};
    case SDL_SCANCODE_APPLICATION: return {0x5D, 0x15D};
    case SDL_SCANCODE_LCTRL: return {kKeyControl, 0x1D};
    case SDL_SCANCODE_RCTRL: return {kKeyControl, 0x11D};
    case SDL_SCANCODE_LSHIFT: return {kKeyShift, 0x2A};
    case SDL_SCANCODE_RSHIFT: return {kKeyShift, 0x36};
    case SDL_SCANCODE_LALT: return {kKeyMenu, 0x38};
    case SDL_SCANCODE_RALT: return {kKeyMenu, 0x138};
    case SDL_SCANCODE_LGUI: return {0x5B, 0x15B};
    case SDL_SCANCODE_RGUI: return {0x5C, 0x15C};
    default: return {0, 0};
    }
}

} // namespace

KeyCode KeyOf(SDL_Scancode scancode, SDL_Keycode typed, SDL_Keymod mod, LayoutKeyFunction layoutKey) {
    KeyCode code = UsKeyOf(scancode, typed, mod);
    if (code.key && layoutKey && LayoutMoves(code.scan))
        if (const int key = layoutKey(code.scan)) code.key = key;
    return code;
}

int ControlCharacter(int key, int scan, SDL_Keymod mod, LayoutControlFunction layoutControl) {
    const bool control = (mod & SDL_KMOD_CTRL) != 0;
    const bool alt = (mod & SDL_KMOD_ALT) != 0;
    if (alt) return 0;
    if (control) {
        if (layoutControl)
            if (const int character = layoutControl(key, scan); character >= 0) return character;
        if (key >= 'A' && key <= 'Z') return key - 'A' + 1;
        if (key == kKeyEscape) return 0x1B;
        if (key == kKeyBack) return 0x7F;
        if (key == kKeyReturn) return 0x0A;
        return 0;
    }
    switch (key) {
    case kKeyBack: return 0x08;
    case kKeyTab: return 0x09;
    case kKeyReturn: return 0x0D;
    case kKeyEscape: return 0x1B;
    default: return 0;
    }
}

} // namespace platform::sdl_keys
