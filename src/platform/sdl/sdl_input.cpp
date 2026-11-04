// SDL's keyboard and mouse events as the platform's Events (sdl_input.h).
#include "platform/sdl/sdl_input.h"

#include "platform/sdl/sdl_keys.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <string>
#include <utility>

namespace platform::sdl_input {
namespace {

std::uint8_t Modifiers(SDL_Keymod mod) {
    return static_cast<std::uint8_t>(((mod & SDL_KMOD_SHIFT) ? kModifierShift : 0) |
                                     ((mod & SDL_KMOD_CTRL) ? kModifierControl : 0) |
                                     ((mod & SDL_KMOD_ALT) ? kModifierAlt : 0));
}

Event TextEvent(std::string text, int scan) {
    Event event;
    event.type = EventType::Text;
    event.scan = scan;
    event.text = std::move(text);
    return event;
}

int Whole(float coordinate) { return static_cast<int>(std::floor(coordinate)); }

Event MouseEvent(EventType type, float x, float y, float pixelsPerPoint) {
    Event event;
    event.type = type;
    event.x = Whole(x * pixelsPerPoint);
    event.y = Whole(y * pixelsPerPoint);
    return event;
}

} // namespace

bool Translator::Take(const SDL_Event& e, float pixelsPerPoint, std::deque<Event>* out) {
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const SDL_KeyboardEvent& k = e.key;
        const sdl_keys::KeyCode code = sdl_keys::KeyOf(k.scancode, k.mod, LayoutKey);
        if (code.key == 0) return true;
        Event event;
        event.type = k.down ? EventType::KeyDown : EventType::KeyUp;
        event.key = code.key;
        event.scan = code.scan;
        event.repeat = k.down && k.repeat;
        event.modifiers = Modifiers(k.mod);
        out->push_back(event);
        if (!k.down) return true;
        typedScan = code.scan;
        if (const int character = sdl_keys::ControlCharacter(code.key, code.scan, k.mod, LayoutControlCharacter)) {
            char text[5] = {};
            SDL_UCS4ToUTF8(static_cast<Uint32>(character), text);
            out->push_back(TextEvent(text, typedScan));
        }
        return true;
    }
    case SDL_EVENT_TEXT_INPUT: out->push_back(TextEvent(e.text.text, typedScan)); return true;
    case SDL_EVENT_MOUSE_MOTION:
        motionRestX += e.motion.xrel * pixelsPerPoint;
        motionRestY += e.motion.yrel * pixelsPerPoint;
        motionX += Whole(motionRestX);
        motionY += Whole(motionRestY);
        motionRestX -= std::floor(motionRestX);
        motionRestY -= std::floor(motionRestY);
        out->push_back(MouseEvent(EventType::MouseMove, e.motion.x, e.motion.y, pixelsPerPoint));
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const SDL_MouseButtonEvent& b = e.button;
        Event event = MouseEvent(b.down ? EventType::MouseDown : EventType::MouseUp, b.x, b.y, pixelsPerPoint);
        switch (b.button) {
        case SDL_BUTTON_LEFT: event.button = MouseButton::Left; break;
        case SDL_BUTTON_RIGHT: event.button = MouseButton::Right; break;
        case SDL_BUTTON_MIDDLE: event.button = MouseButton::Middle; break;
        case SDL_BUTTON_X1: event.button = MouseButton::X1; break;
        case SDL_BUTTON_X2: event.button = MouseButton::X2; break;
        default: return true;
        }
        event.clicks = b.clicks;
        out->push_back(event);
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        const SDL_MouseWheelEvent& w = e.wheel;
        Event event = MouseEvent(EventType::Wheel, w.mouse_x, w.mouse_y, pixelsPerPoint);
        event.wheel = w.direction == SDL_MOUSEWHEEL_FLIPPED ? -w.y : w.y;
        if (event.wheel != 0.0f) out->push_back(event);
        return true;
    }
    default: return false;
    }
}

Point Translator::TakeMotion() { return Point{std::exchange(motionX, 0), std::exchange(motionY, 0)}; }

} // namespace platform::sdl_input
