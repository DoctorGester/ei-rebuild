#pragma once

// Recorded play (harness_record.cpp; docs/notes/harness.md, "Recorded play"): a run's input written out as a scenario
// that replays it, so real play becomes a regression test with a golden.
//
// A scenario line "recordinput" makes the run write recorded.txt in its run folder: the scenario's lines read before the
// game started (the window, the settings, the flags), then one "at <frame> ..." line for each thing that reached the
// game, at the harness's frame count (the main loop's frames; the steps run in the pass after each frame):
//   at <frame> event key down|up <key> <scan> <modifiers> [repeat] [raw]
//   at <frame> event text <scan> <UTF-8, %XX for bytes outside ! .. ~ and for %>
//   at <frame> event move <x> <y> [c] | down|up <button> <x> <y> <clicks> [c] | wheel <x> <y> <notches> [c]
//   at <frame> event motion <dx> <dy>       the mouse's motion (TakeMouseMotion's counts)
//   at <frame> event cursor <x> <y>         the harness's mouse (CursorPosition) moved; "c" on a mouse event: to its point
//   at <frame> <step>                       a step that acts on the game itself (newgame, load, console, zone, quit, ...)
// Every event is the platform's (platform::Event) as the harness queued it, whether a step posted it (key, click,
// clickunit, walkto's clicks, ...) or the player did ("recordinput live": SDL's events through the game's own
// translation, platform/sdl/sdl_input.h). "raw": a key press that types no character of the harness's (real play's text
// is its own event). Waits and checks leave nothing: their timing is in the frame numbers. Replayed on the virtual clock
// the game takes the same input at the same frames and plays the same game.

#include "platform/platform.h"

#include <atomic>
#include <string>
#include <vector>

namespace ei::verify::record {

// "recordinput [live]" (read before the game starts): this run is recorded; "live" takes the player's own keyboard and
// mouse (play.cmd record), its frames paced to real time.
void Ask(bool live);
bool Asked();
bool Live();

// The harness's frame count and mouse (client pixels, -1 before a step placed it), which the lines carry and replay sets.
void Init(const unsigned* frames, std::atomic<int>* mouseX, std::atomic<int>* mouseY);
// The recording begins: recorded.txt opened in the run folder, with `header` (the scenario's lines read before the game
// started) after the marker line "recorded". The error, or "".
std::string Begin(const std::vector<std::string>& header, const std::string& source);
bool On();

// What a step leaves in a recording: its input is recorded as the events it queues (Input), nothing (Skip: waits and
// checks), itself (Kept), or the recording cannot replay it (Refused: a step outside these lists fails the run).
enum class StepKind { Input, Skip, Kept, Refused };
StepKind Classify(const std::string& verb);
// A Kept step, at this frame.
void Step(const std::string& verb, const std::string& argument);

// The harness's input as it is queued (harness_platform.cpp's Queue and AddMotion).
void Queued(const platform::Event& event, bool typesCharacter);
void Motion(int dx, int dy);

// The end of the pass after a frame: the harness's mouse written when it moved, the lines flushed; in live play the
// frames held to real time (the game's virtual clock no faster than the wall clock).
void EndPass();
// The file closed (the game quits).
void End();

// An "at" line's "event ..." on replay: the event queued, the motion added or the mouse placed. The error, or "".
std::string Replay(const std::string& argument);

} // namespace ei::verify::record
