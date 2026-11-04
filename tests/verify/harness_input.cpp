// The harness's input is the game's only input (harness_input.h).
#include "harness_input.h"

#include "platform/harness_platform.h"

#include <string>

namespace ei::verify::input {
namespace {

using platform::Event;
using platform::EventType;
using platform::MouseButton;

// A key message's data: the scan code (bit 8 extended), Alt held, the key down before.
constexpr int kDataScan = 0x1FF, kDataAlt = 0x2000, kDataRepeated = 0x4000;

int DataFlags(std::intptr_t data) { return static_cast<int>((static_cast<std::uintptr_t>(data) >> 16) & 0xFFFF); }

void SetKey(Event& event, EventType type, std::uintptr_t key, std::intptr_t data) {
    const int flags = DataFlags(data);
    event.type = type;
    event.key = static_cast<int>(key);
    event.scan = flags & kDataScan;
    event.repeat = type == EventType::KeyDown && (flags & kDataRepeated) != 0;
    event.modifiers = (flags & kDataAlt) ? platform::kModifierAlt : 0;
}

void SetMouse(Event& event, EventType type, std::intptr_t at) {
    event.type = type;
    event.x = static_cast<short>(at & 0xFFFF);
    event.y = static_cast<short>((at >> 16) & 0xFFFF);
}

void SetButton(Event& event, EventType type, MouseButton button, int clicks, std::intptr_t at) {
    SetMouse(event, type, at);
    event.button = button;
    event.clicks = clicks;
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

void Post(unsigned message, std::uintptr_t word, std::intptr_t data) {
    if (message == Activate) {
        harnessplatform::Activate((word & 0xFFFF) != 0);
        return;
    }
    Event event;
    switch (message) {
    case KeyDown:
    case SystemKeyDown: SetKey(event, EventType::KeyDown, word, data); break;
    case KeyUp:
    case SystemKeyUp: SetKey(event, EventType::KeyUp, word, data); break;
    case Character:
        event.type = EventType::Text;
        event.scan = DataFlags(data) & kDataScan;
        event.text = Utf8(static_cast<unsigned>(word & 0xFFFF));
        break;
    case MouseMove: SetMouse(event, EventType::MouseMove, data); break;
    case LeftDown: SetButton(event, EventType::MouseDown, MouseButton::Left, 1, data); break;
    case LeftUp: SetButton(event, EventType::MouseUp, MouseButton::Left, 1, data); break;
    case LeftDouble: SetButton(event, EventType::MouseDown, MouseButton::Left, 2, data); break;
    case RightDown: SetButton(event, EventType::MouseDown, MouseButton::Right, 1, data); break;
    case RightUp: SetButton(event, EventType::MouseUp, MouseButton::Right, 1, data); break;
    case RightDouble: SetButton(event, EventType::MouseDown, MouseButton::Right, 2, data); break;
    case Wheel:
        SetMouse(event, EventType::Wheel, data);
        event.wheel = static_cast<float>(static_cast<short>((word >> 16) & 0xFFFF)) / 120.0f;
        break;
    default: return;
    }
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
