#pragma once

#include "platform/paths.h"
#include "platform/platform.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The harness's platform backend (this folder; docs/notes/harness.md): every answer of platform.h that a harness's
// build takes in place of the game's own. harness_platform.cpp on SDL takes platform_sdl.cpp's place: a harness game
// takes no real input and shows no box, its window is never activated (and kept out of sight where the system can),
// its events are the harness's, its clocks, mouse and message boxes the harness's answers. game_answers.cpp takes
// game_calls.cpp's and system_path.cpp's: the file paths mapped onto the run's overlay, the game's file writes
// watched, its sockets on 127.0.0.1, the desktop and other-instance answers. Each is the system's call while the
// harness is not answering (a harness's build started without a scenario).

namespace ei::verify::harnessplatform {

// The harness's input: `event` queued for the game's loops.
void Queue(const platform::Event& event);
// The mouse's motion the game's next read reports, (dx, dy) counts more.
void AddMotion(int dx, int dy);
// The game told its window gained or lost the focus (the focus steps, the first activation): a Focus event ahead of
// the input waiting.
void Activate(bool active);
// The system's keyboard and mouse events the backend dropped (for the report).
unsigned RealInputLeftOut();
// The cursor shown over the window (MakeCursor's pixels, R, G, B, A rows from the top) and where HoldCursor holds it;
// false when none is shown / it is not held.
struct ShownCursor {
    int width = 0, height = 0, hotX = 0, hotY = 0;
    std::vector<std::uint8_t> rgba;
};
bool CursorShown(ShownCursor* cursor);
// The shapes SetCursor was given while the harness asked for them (CollectCursors), each once; ForgetSeenCursors starts anew.
const std::vector<ShownCursor>& SeenCursors();
void ForgetSeenCursors();
bool CursorHeld(Point* at);
// The game window's client area in pixels (false before it is made).
bool ClientSize(int* width, int* height);
// The game window minimised or restored, never activated (the focus steps).
void ShowMinimized(bool minimized);
// The game window's client area resized (the windowsize step).
void ResizeClient(int width, int height);
// The game window's title, and its class as the system names it (its registered class where windows have none).
std::string WindowTitle();
std::string WindowClass();

// What the backend asks of the harness (harness.cpp): before the game looks for events (EventWaits, TakeEvent), the
// frame count and the steps, false while the harness holds the events for a pass (the close-all); the character a key
// press types (the US layout, no key held; 0: none), queued when NextEvent hands the press out, as TranslateMessage
// posted it; a message box's answer.
bool BeforeEvents();
// Whether SetCursor keeps the shape it is given (SeenCursors).
bool CollectCursors();
int TypedCharacter(int key);
platform::MessageAnswer AnswerMessageBox(const char* text, const char* caption, platform::MessageButtons buttons);
// The harness's mouse's position on the screen (false: none).
bool MousePosition(Point* at);
// Whether the harness answers the game's calls (a scenario runs), and the path the system is asked for when the game
// names `path` for `use` (the run's overlay: platform::SystemPath's answer).
bool Answering();
std::string MapPath(std::string_view path, platform::PathUse use);

} // namespace ei::verify::harnessplatform
