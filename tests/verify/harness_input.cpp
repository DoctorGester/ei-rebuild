// The harness's input is the game's only input (harness_input.h).
#include "harness_input.h"

#include "platform/harness_platform.h"

#include <string>

namespace ei::verify::input {
namespace {

using platform::Event;
using platform::EventType;
using platform::Key;
using platform::MouseButton;

Event KeyEvent(EventType type, Key key, unsigned scan, std::uint8_t modifiers) {
    Event event;
    event.type = type;
    event.key = key;
    event.scan = static_cast<int>(scan);
    event.modifiers = modifiers;
    return event;
}

Event MouseEvent(EventType type, Point at) {
    Event event;
    event.type = type;
    event.x = at.x;
    event.y = at.y;
    return event;
}

Event ButtonEvent(EventType type, MouseButton button, Point at, int clicks) {
    Event event = MouseEvent(type, at);
    event.button = button;
    event.clicks = clicks;
    return event;
}

// A character (at most U+FFFF) as UTF-8.
std::string Utf8(unsigned c) {
    std::string text;
    if (c < 0x80) {
        text += static_cast<char>(c);
    } else if (c < 0x800) {
        text += static_cast<char>(0xC0 | (c >> 6));
        text += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        text += static_cast<char>(0xE0 | (c >> 12));
        text += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        text += static_cast<char>(0x80 | (c & 0x3F));
    }
    return text;
}

} // namespace

unsigned ScanCode(Key key) {
    // The letters' row by row, as a US keyboard's set 1.
    static const unsigned char kLetters[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                               0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    const auto after = [key](Key first) { return static_cast<unsigned>(key) - static_cast<unsigned>(first); };
    if (key >= Key::A && key <= Key::Z) return kLetters[after(Key::A)];
    if (key > Key::Digit0 && key <= Key::Digit9) return 0x02 + (after(Key::Digit0) - 1);
    if (key == Key::Digit0) return 0x0B;
    if (key >= Key::F1 && key < KeyAfter(Key::F1, 10)) return 0x3B + after(Key::F1);
    if (key == KeyAfter(Key::F1, 10)) return 0x57;
    if (key == Key::F12) return 0x58;
    switch (key) {
    case Key::Backspace: return 0x0E;
    case Key::Tab: return 0x0F;
    case Key::Return: return 0x1C;
    case Key::Shift: return 0x2A;
    case Key::Control: return 0x1D;
    case Key::Alt: return 0x38;
    case Key::Escape: return 0x01;
    case Key::Space: return 0x39;
    // The navigation keys as the numeric keypad's (without the extended bit, as MapVirtualKey's VK_TO_VSC).
    case Key::PageUp: return 0x49;
    case Key::PageDown: return 0x51;
    case Key::End: return 0x4F;
    case Key::Home: return 0x47;
    case Key::Left: return 0x4B;
    case Key::Up: return 0x48;
    case Key::Right: return 0x4D;
    case Key::Down: return 0x50;
    case Key::Delete: return 0x53;
    case Key::KeypadMultiply: return 0x37;
    case Key::KeypadPlus: return 0x4E;
    case Key::KeypadMinus: return 0x4A;
    case Key::KeypadDivide: return 0x35;
    case Key::ScrollLock: return 0x46;
    case Key::Semicolon: return 0x27;
    case Key::Equals: return 0x0D;
    case Key::Comma: return 0x33;
    case Key::Minus: return 0x0C;
    case Key::Period: return 0x34;
    case Key::Slash: return 0x35;
    case Key::Grave: return 0x29;
    case Key::LeftBracket: return 0x1A;
    case Key::Backslash: return 0x2B;
    case Key::RightBracket: return 0x1B;
    case Key::Apostrophe: return 0x28;
    default: return 0;
    }
}

void PressKey(Key key, unsigned scan, std::uint8_t modifiers) {
    // Alt+F4 is the window's close request, as the game's backend gives it (sdl_input.h, IsCloseKey).
    if (key == Key::F4 && (modifiers & platform::kModifierAlt) && !(modifiers & platform::kModifierControl)) {
        Event close;
        close.type = EventType::CloseRequested;
        harnessplatform::Queue(close);
        return;
    }
    harnessplatform::Queue(KeyEvent(EventType::KeyDown, key, scan, modifiers));
}

void ReleaseKey(Key key, unsigned scan, std::uint8_t modifiers) {
    harnessplatform::Queue(KeyEvent(EventType::KeyUp, key, scan, modifiers));
}

void TypeCharacter(unsigned character, unsigned scan) {
    Event event;
    event.type = EventType::Text;
    event.scan = static_cast<int>(scan);
    event.text = Utf8(character);
    harnessplatform::Queue(event);
}

void MoveMouse(Point at) { harnessplatform::Queue(MouseEvent(EventType::MouseMove, at)); }

void PressButton(MouseButton button, Point at, int clicks) {
    harnessplatform::Queue(ButtonEvent(EventType::MouseDown, button, at, clicks));
}

void ReleaseButton(MouseButton button, Point at) { harnessplatform::Queue(ButtonEvent(EventType::MouseUp, button, at, 1)); }

void TurnWheel(int turn, Point at) {
    Event event = MouseEvent(EventType::Wheel, at);
    event.wheel = static_cast<float>(turn) / 120.0f;
    harnessplatform::Queue(event);
}

void AddMotion(int dx, int dy) { harnessplatform::AddMotion(dx, dy); }

void Report(FILE* f) {
    const unsigned left = harnessplatform::RealInputLeftOut();
    if (!left)
        std::fprintf(f, "\nMOUSE: the harness's only (the real keyboard and mouse never reached the game window)\n");
    else
        std::fprintf(f, "\nMOUSE: the harness's only; the real keyboard and mouse left out: %u events\n", left);
}

} // namespace ei::verify::input
