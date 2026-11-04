#pragma once

// SDL's keyboard and mouse events as the platform's Events (sdl_input.cpp; docs/notes/platform.md, "SDL3"): the game's
// SDL backend (platform_sdl.cpp) and the harness's recording of real play (tests/verify/harness_record.h) take the
// player's input through this one translation, so a recorded session holds what the game is given.

#include "platform/platform.h"

#include <SDL3/SDL_events.h>

#include <cstdint>
#include <deque>

namespace platform::sdl_input {

class Translator {
public:
    // The events `e` makes, appended to `out`: a key with its code, scan code, repeat and modifiers and the control
    // character it types (sdl_keys.h), typed text with the scan code of the last key pressed, a mouse move, a button
    // with its click count, a wheel turn in notches; positions in pixels (`pixelsPerPoint`: SDL's coordinates are
    // points). False for an event that is not the keyboard's or the mouse's.
    bool Take(const SDL_Event& e, float pixelsPerPoint, std::deque<Event>* out);
    // The motion SDL's mouse events carried since the last call, in pixels; the fractions are kept for the next event
    // (the held cursor's relative mode scales the motion as the system scales the pointer's).
    Point TakeMotion();

private:
    int typedScan = 0;
    std::int32_t motionX = 0, motionY = 0;
    float motionRestX = 0.0f, motionRestY = 0.0f;
};

} // namespace platform::sdl_input
