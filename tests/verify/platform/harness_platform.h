#pragma once

#include "platform/seams.h"
#include "platform/platform.h"

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

// The harness's platform backend (this folder; docs/notes/harness.md): every answer of platform.h that a harness's
// build takes in place of the game's own. harness_platform.cpp on SDL takes platform_sdl.cpp's place: a harness game
// takes no real input and shows no box, its window is never activated (and kept out of sight where the system can),
// its events are the harness's, its clocks, mouse and message boxes the harness's answers. game_answers.cpp answers the
// platform's seams in place of seams.cpp: the file paths mapped onto the run's overlay, the game's file writes watched,
// the run isolated from the desktop (no other game). game_sockets.cpp takes the sockets' place:
// the game's sockets on 127.0.0.1. Each is the game's answer while the harness is not answering (a harness's build
// started without a scenario).

namespace ei::verify::harnessplatform {

// The harness's input: `event` queued for the game's loops; with `typesCharacter` a key press also gets the character
// TypedCharacter names when the game takes it (the harness's keys; real play's text comes with its own events). The
// recorder sees it (harness_record.h).
void Queue(const platform::Event& event, bool typesCharacter = true);
// The mouse's motion the game's next read reports, (dx, dy) counts more.
void AddMotion(int dx, int dy);
// Real play (the recorder's live mode, harness_record.h): from now on SDL's keyboard and mouse events go through the
// game's own translation (platform/sdl/sdl_input.h) and wait for TakeReal instead of being dropped; the game window is
// shown, activated and of a fixed size, the text input on, the cursor held and placed for real. Before the window
// system starts.
void TakeRealInput();
// The real events and the mouse's motion (pixels) since the last call (false: none).
bool TakeReal(std::deque<platform::Event>* events, Point* motion);
// The player closed the window in real play.
bool CloseRequested();
// Input made as SDL's own and translated as real play's (the "realinput" step): a key pressed and released (SDL's
// name: "A", "Return", "Escape", "Left"), typed text, the mouse moved to a point of the window's client area (pixels)
// and there clicked.
void InjectRealKey(const char* name);
void InjectRealText(const std::string& text);
enum class RealMouse { Move, Click, RightClick };
void InjectRealMouse(RealMouse what, Point at);
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
int TypedCharacter(platform::Key key);
// The game asked to quit (PostQuit): a recorded scenario's game may quit itself after its last step.
void GameQuits();
platform::MessageAnswer AnswerMessageBox(std::string_view text, std::string_view caption, platform::MessageButtons buttons);
// The harness's mouse's position on the screen (false: none).
bool MousePosition(Point* at);
// Whether the harness answers the game's calls (a scenario runs), and the path the system is asked for when the game
// names `path` for `use` (the run's overlay: platform::SystemPath's answer), and the searches that make up a folder
// search (platform::SystemSearches').
bool Answering();
std::string MapPath(std::string_view path, platform::PathUse use);
std::vector<platform::FolderSearch> MapSearches(std::string_view pattern);

} // namespace ei::verify::harnessplatform
