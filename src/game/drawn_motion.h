#pragma once

// The drawn motion of map objects (docs/notes/renderer.md section 14, "Objects"): what moves only in client
// ticks (a bow's arrow, an object or unit the server's updates move) is drawn less the part of its last tick's
// step the frame has not reached yet, as the camera's drawn view is. Units placed on their path at the frame's
// time already move smoothly and are drawn where they are. Only the draw reads it: the object's own pose (what
// the game, the digests and the saves see) never changes. The lights likewise: an object's own light is drawn by
// its drawn place, any other light less the part of its last tick's step not reached; localPosition stays the game's.

#include "lib/vecmath.h"

#include <cstdint>

class Light;
class Object3D;

namespace drawn {

struct Pose {
    Vector3 position;
    Quaternion rotation;
};

// An object's last tick step as the frames observe it (Object3D::drawnStep): the pose at the observation after the
// tick before and after the last one, the tick count of the last observation, whether the frame moved it since
// (a unit placed on its path) and whether the step is drawn. And a unit's path change: the place last shown, the
// path changed since, the offset from the new place to the one shown before it and when it began to fade.
struct ObjectStep {
    Pose from{};
    Pose to{};
    std::uint32_t seenTick = 0;
    bool placedByFrame = false;
    bool moving = false;
    Vector3 shown{};
    bool hasShown = false;
    bool pathChanged = false;
    Vector3 correction{};
    float correctionShare = 0.0f;
    std::uint32_t correctionStart = 0;
};

// A light's last tick step as the frames observe it (Light::drawnStep), as ObjectStep's, and the place an object
// carrying it gave it in the pass `carriedPass`.
struct LightStep {
    Vector3 from{};
    Vector3 to{};
    std::uint32_t seenTick = 0;
    bool moving = false;
    Vector3 carried{};
    std::uint32_t carriedPass = 0;
};

// After each client tick (RunWorlds).
void TickRan();
// The part of a tick the frames spread a tick's step over after a pass of `passTicks` ticks: all of it after a pass
// of up to a tick, less after a longer one, none from two ticks on (each frame sees whole ticks anyway).
float PassSpread(float passTicks);
// Once a pass, after the worlds ran: the part of a tick the client has had since its last tick (clamped to 0..1)
// and the pass's real time in ticks (as camera::SetViewProgress).
void SetProgress(float reached, float passTicks);
// The part of the last tick's step the frame has not reached yet (0..1).
float Back();

// The frame's look at `object` before anything places it at the frame's time (WorldClient::CollectVisible): the
// first look after a tick takes the step the tick made.
void Observe(Object3D* object);
// After a frame's placement of `object` (a unit's AdvanceTime): a pose changed from `before` is the frame's. After
// a path change the jump from the place shown last fades out over a short time instead of showing at once.
void NotePlaced(Object3D* object, const Pose& before);
// A unit's client path changed (UnitClientSpecific::MotionChanged): its next placement may jump.
void NotePathChanged(Object3D* object);
// The object's pose now.
Pose OwnPose(const Object3D* object);
// The pose the draw shows: its own less the part of its last tick's step not reached yet, or its own when it has no
// step, the frame placed it, or nothing is left to take back; plus what is left of a path change's jump. An
// interface figure's (MapObjectFlag::NoEdgeFade during its draw) is its own: the interface placed it.
Pose PoseOf(const Object3D* object);
// PoseOf, once a frame where the world's draw places the object (CollectVisible): the place is kept as shown.
Pose Show(Object3D* object);

// The world's draw placed the object carrying `light` (an object's own light, MapObjectClientSpecific::light): the
// light is drawn at `at` this pass.
void Carry(Light* light, const Vector3& at);
// The place the draw shows `light` at this pass: where its carrier put it (Carry), else its own less the part of its
// last tick's step not reached (the first look after a tick takes the step, as Observe). Every reader of a pass gets
// the same place (the GPU's point lights match the meshes' by it).
Vector3 LightPosition(Light* light);

} // namespace drawn
