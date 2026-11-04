// The harness's input is the game's only input (harness_input.h).
#include "harness_input.h"

#include "platform/harness_platform.h"

#include <string>

namespace ei::verify::input {
namespace {

using platform::Event;
using platform::EventType;
using platform::MouseButton;

Event KeyEvent(EventType type, unsigned key, unsigned scan, std::uint8_t modifiers) {
    Event event;
    event.type = type;
    event.key = static_cast<int>(key);
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

unsigned ScanCode(unsigned key) {
    // The letters' row by row, as a US keyboard's set 1.
    static const unsigned char kLetters[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                               0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    if (key >= 'A' && key <= 'Z') return kLetters[key - 'A'];
    if (key >= '1' && key <= '9') return 0x02 + (key - '1');
    if (key == '0') return 0x0B;
    if (key >= kKeyF1 && key < kKeyF1 + 10) return 0x3B + (key - kKeyF1);
    if (key == kKeyF1 + 10) return 0x57;
    if (key == kKeyF1 + 11) return 0x58;
    switch (key) {
    case kKeyBack: return 0x0E;
    case kKeyTab: return 0x0F;
    case kKeyReturn: return 0x1C;
    case kKeyShift: return 0x2A;
    case kKeyControl: return 0x1D;
    case kKeyMenu: return 0x38;
    case kKeyEscape: return 0x01;
    case kKeySpace: return 0x39;
    // The navigation keys as the numeric keypad's (without the extended bit, as MapVirtualKey's VK_TO_VSC).
    case kKeyPageUp: return 0x49;
    case kKeyPageDown: return 0x51;
    case kKeyEnd: return 0x4F;
    case kKeyHome: return 0x47;
    case kKeyLeft: return 0x4B;
    case kKeyUp: return 0x48;
    case kKeyRight: return 0x4D;
    case kKeyDown: return 0x50;
    case kKeyDelete: return 0x53;
    case kKeyMultiply: return 0x37;
    case kKeyAdd: return 0x4E;
    case kKeySubtract: return 0x4A;
    case kKeyDivide: return 0x35;
    case kKeyScroll: return 0x46;
    case kKeyOem1: return 0x27;
    case kKeyOemPlus: return 0x0D;
    case kKeyOemComma: return 0x33;
    case kKeyOemMinus: return 0x0C;
    case kKeyOemPeriod: return 0x34;
    case kKeyOem2: return 0x35;
    case kKeyOem3: return 0x29;
    case kKeyOem4: return 0x1A;
    case kKeyOem5: return 0x2B;
    case kKeyOem6: return 0x1B;
    case kKeyOem7: return 0x28;
    default: return 0;
    }
}

void PressKey(unsigned key, unsigned scan, std::uint8_t modifiers) {
    harnessplatform::Queue(KeyEvent(EventType::KeyDown, key, scan, modifiers));
}

void ReleaseKey(unsigned key, unsigned scan, std::uint8_t modifiers) {
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
