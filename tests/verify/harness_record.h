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

// "recordinput [live] [cut <frames>]" (read before the game starts): this run is recorded; "live" takes the player's own
// keyboard and mouse (harness.cmd record), its frames paced to real time; "cut": a part of this many frames is cut off
// ("Parts" below).
void Ask(bool live, unsigned cutFrames = 0);
bool Asked();
bool Live();

// The frame the "at" lines count: the harness's frame count less the part's origin (the "origin" step).
unsigned Frame();

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

// Parts. A recording that runs long is cut into parts that each replay within a run's 60 s (docs/notes/harness.md,
// "Recorded play"): at a cut the run presses F5 (the game's quick save, "saveq"), the part's file ends with a quit, and
// the run loads the save at once (steps load, waitwindow battle, origin) while the next part's file, recorded_<n>.txt,
// begins with the same lines, from the main menu: a "# prepare: recorded_save.py" line (the save made by the replay of
// the part before), the kept settings, those steps. Its "at" lines count from the "origin" step, the frame where the
// loaded game settled. Nothing is recorded between the cut and the origin (the player's input then is dropped).
// A cut is due once the part is "cut" frames long and the player holds no key or button; the "recordcut" step cuts
// where a scenario says.
bool CutDue();
bool Cutting();
// The part's file ends (a quit line) and the next part's begins. The error, or "".
std::string Part();
// The "origin" step: the frames count from here. Ends a cut under way.
void Origin();

// The end of the pass after a frame: the harness's mouse written when it moved, the lines flushed; in live play the
// frames held to real time (the game's virtual clock no faster than the wall clock).
void EndPass();
// The file closed (the game quits).
void End();

// An "at" line's "event ..." on replay: the event queued, the motion added or the mouse placed. The error, or "".
std::string Replay(const std::string& argument);

} // namespace ei::verify::record
