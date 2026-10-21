#pragma once

// The camera's motion frame by frame (verify build only; harness_camera.cpp, R18): the smooth camera's
// check (render::SmoothCamera, docs/notes/renderer.md section 14). Steps:
//   camtrace start            record the battle camera's focus, heading and distance after each frame's
//                             last camera update from now on (the main loop's per-frame call, which the
//                             harness takes over for this: HeldCameraUpdate)
//   camtrace stop <name>      end the recording and keep it as <name>: per kind (pan: the focus' path
//                             on the map, turn: the heading's, in radians, zoom: the distance's) the
//                             total, the largest step and the jumps: frames whose step is more than
//                             twice both neighbours' (logged, and in the report)
//   camsame <a> <b> <kind> <fraction>
//                             require recordings <a> and <b> to have moved by the same total of <kind>
//                             (pan, turn or zoom) within <fraction> of the larger, and by something
//   camsmooth <name> <kind> [jerky]
//                             require recording <name> to have moved in <kind>, without a jump (with
//                             "jerky": with at least one, the game's camera without the option at a
//                             frame time shorter than a tick)
// Skipped in a run of the original code (the option is the image's).

#include <string>
#include <vector>

namespace ei::verify::camera_trace {

// After the main loop's last camera update of a frame (`camera`: the CCamera; `seconds`: that call's
// camera time).
void Frame(const void* camera, float seconds);

// Whether the scenario's steps use the recording (the per-frame camera call is then taken over).
bool UsedBy(const std::string& verb);

// Runs one of the steps above (`verb` as UsedBy accepts). A failed check's text goes to `failures`,
// every result line to `lines` (the report).
void Step(const std::string& verb, const std::string& argument, std::vector<std::string>* failures,
          std::vector<std::string>* lines);

} // namespace ei::verify::camera_trace
