#pragma once

#include "platform/platform.h"

#include <cstdint>

// The harness's own platform backend on SDL (harness_platform.cpp): what the harness's builds take in place of the
// game's (src/platform/sdl/platform_sdl.cpp). A harness game takes no real input and shows no box: its window is never
// activated, its events are the harness's, its clocks, mouse and message boxes the harness's answers.

namespace ei::verify::harnessplatform {

// The harness's input: `event` queued for the game's loops.
void Queue(const platform::Event& event);
// The mouse's motion the game's next read reports, (dx, dy) counts more.
void AddMotion(int dx, int dy);
// The game told its window was activated or deactivated (the focus steps, the first activation).
void Activate(bool active);
// The system's keyboard and mouse events the backend dropped (for the report).
unsigned RealInputLeftOut();

// What the backend asks of the harness (harness.cpp): before the game looks for events (EventWaits, TakeEvent), the
// frame count and the steps, false while the harness holds the events for a pass (the close-all); the character a key
// press types (the US layout, no key held; 0: none), queued when NextEvent hands the press out, as TranslateMessage
// posted it; a message box's answer.
bool BeforeEvents();
int TypedCharacter(int key);
platform::MessageAnswer AnswerMessageBox(const char* text, const char* caption, platform::MessageButtons buttons);
// The harness's clocks, sleep and date (its virtual or held clocks, its fixed date) and its mouse's position on the
// screen (false: none), and a move of it.
std::uint32_t ClockMilliseconds();
std::uint32_t ClockTicks();
void ClockSleep(std::uint32_t ms);
platform::DateTime Date();
bool MousePosition(platform::Point* at);
void MoveMouse(platform::Point at);

} // namespace ei::verify::harnessplatform
