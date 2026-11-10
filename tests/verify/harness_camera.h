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
// The objects' drawn motion (drawn::PoseOf, R19; renderer.md section 14, "Objects") the same way, sampled after
// each frame's draw (the main loop's frame step, taken over for this: TracedFrameStep):
//   objtrace start <who> [part|-] [log]
//                             follow `who` ("leader", a script name, "arrow": the first bow arrow in flight,
//                             then that one until it is gone, or "spelllight": the first moving point light
//                             effect, a spell's or a missile's, likewise) or its part's place in the figure (e.g.
//                             "rh3", the right hand: the pose alone, its blends) or "light", its own light (own: the
//                             game's place, drawn: drawn::LightPosition) or "lastdraw", where its last draw put it
//                             (both: its world matrix's place; after a frame the HUD unit panel's draw of the unit
//                             it shows, R19b); with "log" each frame's places go to the log ("harness: objframe ...")
//   objtrace stop <name>      keep the drawn place as <name> and the object's own as <name>.own (kinds pan:
//                             the path on the map, turn: the yaw, height); camsame / camsteadier / camsmooth
//                             check them
//   objplayed <animation>     require the last objtrace's object to have played <animation> in a traced frame
//   objplayed <animation> past <frame>   ... and to have reached a frame of it beyond <frame>
//                             (its database entry's name, "uspecial22")
//   animblend on|off          animation switches blend (the default) or snap as in the original
//                             (FigureAnimationState::SetBlends)
//   animblend mark            start counting the blends begun in world figures (not interface previews)
//   animblend count <least> <most>
//                             require the blends begun since the mark (or the run's start) to be in that range

#include <string>
#include <vector>

namespace ei::verify::camera_trace {

// After the main loop's last camera update of a frame (`camera`: the Camera; `seconds`: that call's
// camera time).
void Frame(const void* camera, float seconds);

// After each frame's draw (the main loop's frame step): the object trace's sample.
void ObjectFrame();

// Whether the scenario's steps use the recording (the per-frame camera call is then taken over).
bool UsedBy(const std::string& verb);
// Whether they use the object trace (the frame step is then taken over).
bool ObjectsUsedBy(const std::string& verb);

// Runs one of the steps above (`verb` as UsedBy accepts). A failed check's text goes to `failures`,
// every result line to `lines` (the report).
void Step(const std::string& verb, const std::string& argument, std::vector<std::string>* failures,
          std::vector<std::string>* lines);

} // namespace ei::verify::camera_trace
