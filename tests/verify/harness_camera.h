#pragma once

// The camera's motion frame by frame (verify build only; harness_camera.cpp, R18, R35a): the drawn view's checks
// (camera::DrawnView, docs/notes/renderer.md section 14). Each frame samples the pose the world's draw shows and
// the camera's own. Steps:
//   camtrace start [log]      record after each frame's last camera update from now on (the main loop's
//                             per-frame call, which the harness takes over for this: HeldCameraUpdate); with
//                             "log" each frame's poses go to the log ("harness: camframe ...")
//   camtrace stop <name>      end the recording and keep the drawn view as <name> and the camera's own as
//                             <name>.camera: per kind (pan: the focus' path on the map, turn: the heading's, in
//                             radians, zoom: the distance's, height: the camera's height, reach: its distance
//                             from the focus) the total, the largest step and the jumps: frames whose step is
//                             more than twice both neighbours' (logged, and in the report)
//   camsame <a> <b> <kind> <fraction>
//                             require recordings <a> and <b> to have moved by the same total of <kind>
//                             within <fraction> of the larger, and by something; e.g. the drawn view's reach
//                             against the camera's own in a spin (a step spread along the chord pulls it in and out)
//   camsteadier <a> <b> <kind> <fraction>
//                             require recording <a>'s largest step of <kind> to be at most <fraction> of <b>'s
//                             (and <b> to have moved), e.g. the drawn height against the camera's own, where
//                             the game's own small changes within a frame (no jump of a tick) are left
//   camsmooth <name> <kind> [jerky]
//                             require recording <name> to have moved in <kind>, without a jump (with
//                             "jerky": with at least one, e.g. the camera's own at a frame time shorter than a
//                             tick, where each client tick moves it by a tick at once)

#include <string>
#include <vector>

namespace ei::verify::camera_trace {

// After the main loop's last camera update of a frame (`camera`: the Camera; `seconds`: that call's
// camera time).
void Frame(const void* camera, float seconds);

// Whether the scenario's steps use the recording (the per-frame camera call is then taken over).
bool UsedBy(const std::string& verb);

// Runs one of the steps above (`verb` as UsedBy accepts). A failed check's text goes to `failures`,
// every result line to `lines` (the report).
void Step(const std::string& verb, const std::string& argument, std::vector<std::string>* failures,
          std::vector<std::string>* lines);

} // namespace ei::verify::camera_trace
