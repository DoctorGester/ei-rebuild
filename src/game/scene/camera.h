#pragma once

// The camera (Camera, class id 0x43), the settings record it reads, its saved views, shakes and recorded
// paths, and the engine globals and containers its code uses.

#include "game/core/core_maths.h"
#include "game/core/core_streams.h"
#include "game/core/core_transform.h"
#include "lib/vecmath.h"
#include "game/data/options.h"


#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Object3D;

// Zoom distance limits of one camera mode. Inside [min, max] the distance moves freely; between
// max and hardMax (min and hardMin) it eases back; beyond the hard limits it is cut.
struct CameraLimits {
    float min = 0.0f;
    float max = 0.0f;
    float hardMin = 0.0f;
    float hardMax = 0.0f;
};

struct CameraView {
    float distance = 0.0f;
    Quaternion rotation;
    float pitch = 0.0f;
};
constexpr bool NoPadding(const CameraView*) {
    return lib::FieldsFill<&CameraView::distance, &CameraView::rotation, &CameraView::pitch>();
}

// A shake of the camera (Camera::shakes).
struct CameraShake {
    float elapsed = 0.0f;
    float duration = 0.0f;
    float amplitude = 0.0f;
    float decay = 0.0f;
};

// A key of a recorded camera path (camera\*.cam, played by PlayCamera), and a frame of the file
// being recorded (the dword at +4 is never written by the recorder: stack bytes there, zero here).
struct CameraKey {
    float time = 0.0f;
    std::uint32_t reserved = 0; // 0 here (see above)
    Vector3 position;
    Quaternion rotation;
};
static_assert(sizeof(CameraKey) == 0x24);   // read as the camera file's records
constexpr bool NoPadding(const CameraKey*) {
    return lib::FieldsFill<&CameraKey::time, &CameraKey::reserved, &CameraKey::position, &CameraKey::rotation>();
}

struct RecordFile;
// The keys of a camera path file: the top TOML file's when one stands over the original (docs/notes/mods.md,
// "Records as TOML": [[key]] tables of time, position = [x, y, z] and rotation = [w, x, y, z]; the file is the
// whole path, nothing merged), else the original's records (as many as it holds whole). CameraKeysToml: the keys
// as such a file (without `reserved`, which only a recording's stack bytes fill).
std::vector<CameraKey> ReadCameraKeys(const RecordFile& file);
std::string CameraKeysToml(const std::vector<CameraKey>& keys);

// Average, lowest and highest surface height (the water at rest, Terrain::CalmSurfaceHeight: the camera does not bob
// with the waves) on a grid of `step` spacing over [p.x - halfX, p.x + halfX] x [p.y - halfY, p.y + halfY] (points off
// the map skipped; the centre counts once more).
struct TerrainSample {
    float average = 0.0f;
    float minimum = 0.0f;
    float maximum = 0.0f;
};
TerrainSample SampleTerrain(const Vector3& p, float step, float halfX, float halfY);

// The constants the camera code uses: the zero vector and the identity rotation it copies, ninety degrees in radians
// and the defaults of the near / far distances and the field of view.
namespace camera {
inline constexpr Vector3 kZeroVector = /*0079B5C8*/ {0.0f, 0.0f, 0.0f};
inline constexpr Quaternion kIdentityRotation = /*0079B5B8*/ {1.0f, 0.0f, 0.0f, 0.0f};
inline constexpr float kRightAngle = ei::kRadiansPerDegree * 90.0f;
inline constexpr float kDefaultNear = 1.0f;
inline constexpr float kDefaultFieldOfView = kPi * (2.0f / 7.0f);
// The projection's shape (renderer_init.cpp): the field of view spans the window's 4:3 part across, the axis swap
// widens it to the window (screen_layout.h).
inline constexpr float kAspect = 4.0f / 3.0f;
}   // namespace camera

// The camera ("Camera"). flags: 0x8 following the target, 0x10 a camera file is open, 0x20 playing a
// camera file, 0x40 free (scripted) placement, 0x100 focus kept inside a circle, 0x200 the alternate limits.
class Camera final : public TransformObject {
public:
    Camera();
    ~Camera() override;

    void Create() override;
    void Destroy() override;
    bool RunTick() override { return true; }
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    // Starts recording into ".\camera\" + `name`; false when the file cannot be made. Appends a frame at `time`.
    // CloseFile stops recording (the file name emptied).
    bool OpenFile(std::string_view name);
    void RecordFrame(float time);
    void CloseFile();

    static constexpr std::uint32_t kFollowing = 0x8;
    static constexpr std::uint32_t kRecording = 0x10;
    static constexpr std::uint32_t kPlaying = 0x20;
    static constexpr std::uint32_t kFree = 0x40;
    static constexpr std::uint32_t kPlaced = kPlaying | kFree;   // placed by its own position, not the focus
    static constexpr std::uint32_t kBounded = 0x100;
    static constexpr std::uint32_t kAlternate = 0x200;

    // Per-frame update: listener, target tracking, pan / turn / tilt / zoom motion, limits, ApplyView, shake.
    void Update(float dt);
    // Places the camera at `distance` from the focus along the view rotation tilted by
    // the pitch, then lifts it over objects and re-aims at the focus when it was lifted.
    void ApplyView();
    // Moves the focus to `p`, its height set from the terrain around it.
    void SetFocus(const Vector3& p);
    // Pan velocity target from a screen-space direction (x right, y down the screen), along the ground.
    void Pan(const Vector3& direction);
    void StopMotion();
    // Zoom by 1.05^steps, clamped to the current limits.
    void ZoomBy(float steps);
    // Saved views 0-7 (false for other slots). Restore sets distance and pitch only.
    bool SaveView(unsigned slot);
    bool RestoreView(unsigned slot);
    void SetDistancePitch(const CameraView* view);
    // The four saved views to / from the options' camera shortcuts (rotation as axis and angle).
    void ViewsToSettings(options::GameOptions* settings);
    void ViewsFromSettings(const options::GameOptions* settings);
    // The distance limits of the current mode: the options' briefing limits in the alternate mode (flag
    // 0x200), else the camera's.
    CameraLimits Limits() const;
    // View rotation = `angle` radians about Z, then ApplyView.
    void SetHeading(float angle);
    void SetFree(bool on);
    // Free placement at `from` looking at `to`.
    void LookFrom(const Vector3& from, const Vector3& to);
    void SetBounds(const Vector3& center, float radius);
    // Starts a shake from an event at `source` (ignored 20 or more away).
    void Shake(const Vector3& source, float duration, float amplitude, float decay);
    // Recorded camera path: jump to the first key (false without keys) / interpolate at `time` (nothing when finished
    // or not playing).
    bool PlaybackStart();
    void PlaybackStep(float time);

    // Views and start placement from the settings; the near distance and the field of view are kept in
    // nearDistance and fieldOfView.
    void Init(options::GameOptions* settings, float nearClip, float fov);

    // Lifts the camera over the terrain and the objects within 16 of it, its near plane included.
    void ClampAboveObjects();
    // How far the near plane's corners lie from the camera's point: the projection's near distance and field of view
    // (nearDistance, fieldOfView, camera::kAspect) on the window's shape now (screen_layout's axis scales). No part of
    // the near plane is lower under the camera's point than this, whichever way the camera looks.
    float NearPlaneReach() const;
    // Turns the camera a tenth of the way upright, then ClampAboveObjects.
    void AlignToTarget();
    // Aims along the direction; false (nothing done) for a zero-length one.
    bool LookDirection(float dx, float dy, float dz);

    // Where the camera's view axis (rotated Z) meets the ground plane z = 0; the camera
    // position itself when the axis is (nearly) horizontal or points away from the plane.
    void GroundPoint(Vector3* out) const;

    // Set a target rate; the current rate jumps up to it when larger in magnitude.
    void SetTurnRate(float rate);
    void SetTiltRate(float rate);
    void SetZoomTarget(float value);

    // Follows the object with this id (-1: none); starting to follow resets
    // the two offset vectors to the zero vector.
    void SetTarget(const EngineObject* target);
    void SetTargetId(int id);

    // The point the camera is placed by: its own position when playing a file or placed freely, else the focus.
    const Vector3* Anchor() const { return (flags & kPlaced) ? &localPosition : &focus; }

    // Reads the recorded path <data folder>camera\<name> into the keys (as many as the file holds whole; the mod
    // folder's copy when CameraPatch is on). With `settle` above 1 and below the key count, the keys before the first
    // gap of more than 500 time units among the first `settle` take the pose of the key after the gap. False when
    // the file does not open.
    bool LoadPath(std::string_view name, int settle);

    options::GameOptions* settings = nullptr;    // the game options: heights, start placement, limits, saved views
    Vector3 panVelocity = camera::kZeroVector;
    Vector3 panTarget = camera::kZeroVector;     // pan velocity target
    float panResponse = 2.0f / 15.0f;
    float turnRate = 0.0f;
    float turnTarget = 0.0f;
    float turnResponse = 1.0f / 3.0f;
    float tiltRate = 0.0f;
    float tiltTarget = 0.0f;
    float tiltResponse = 1.0f / 3.0f;
    float zoomRate = 0.0f;
    float zoomTarget = 0.0f;    // the zoom rate target (SetZoomTarget)
    float zoomResponse = 1.0f / 3.0f;
    Vector3 focus = {10.0f, 10.0f, 2.0f};   // the point looked at
    float focusHeight = 2.0f;               // above the ground
    float distance = 0.0f;
    Quaternion viewRotation = camera::kIdentityRotation;   // heading
    float pitch = 0.0f;                     // degrees
    std::vector<CameraShake> shakes;
    Vector3 boundsCenter = camera::kZeroVector;
    float boundsRadius = 0.0f;
    int targetId = -1;
    CameraView views[8];
    float nearDistance = camera::kDefaultNear;   // (renderer_init's projection reads the two; it has no far plane)
    float fieldOfView = camera::kDefaultFieldOfView;
    std::string fileName;      // the camera file being recorded
    std::vector<CameraKey> keys;   // the recorded path's keys
    float timeOffset = 0.0f;
    int keyIndex = 0;
};

namespace camera {

// The drawn view (docs/notes/renderer.md section 14): the camera keeps the game's timing (each client tick
// moves it by a tick, each frame by its time), and the world's draw shows its pose less the part of the last
// tick's step the frame has not reached yet, so the tick's jump is spread over the frames up to the next tick.
struct ViewPose {
    Vector3 position;       // TransformObject::localPosition
    Quaternion rotation;    // TransformObject::localRotation
    Vector3 focus;          // focus, heading viewRotation and distance: what the harness's camtrace reads
    Quaternion heading;
    float distance = 0.0f;
};
// Around a client tick's camera update: the pose before it, then the step it made.
void NoteViewBeforeTick(const Camera* camera);
void NoteViewTick(const Camera* camera);
// A placement of the camera outside its update (a focus, heading or view set, a scripted placement, a load, a
// recorded path's start): the last step is forgotten, so the next frame draws the placed pose. Until a frame draws
// the world, the ticks' steps are cuts and a new target is drawn as reached (Camera::SetTargetId, SettleBy).
void ForgetViewStep();
// The drawn view's settle onto a target taken before the first frame after a placement (Camera::SetTargetId): `gap`
// from the camera's own focus to the settled one, added to the drawn focus and position; the camera's update shrinks
// it as its drift (x, y) and height lag (z, `height`) close the same gap (`keep`: the part left; 0 when the drift
// stops: no target, a pan), and drops it once it is tiny, or when the camera is placed by its own position.
void SettleBy(const Vector3& gap);
void ForgetSettle();
void ShrinkSettle(float keep, bool height);
// Once a pass, after the worlds ran: the part of a tick the client has had since its last tick (its backlog over
// a tick; clamped to 0..1) and the pass's real time in ticks (TickLength's: 27 ms in fast mode). The first pass
// after a tick sets how much of its step the frames spread: all of it after a pass of up to a tick, less after a
// longer one (fewer frames than ticks: each frame sees whole ticks anyway), none from two ticks on (the camera's
// own pose is drawn); later passes only reach further into it, so the drawn view never steps back.
void SetViewProgress(float reached, float passTicks);
// The pose the world's draw shows: the camera's own less the part of its last tick's step not reached yet.
ViewPose DrawnView(const Camera* camera);
// WorldClient::Render takes the frame's drawn view once at its start (TakeFrameView); what the world's draw
// places by the camera reads it from FrameView: the view matrix, the sky, particle billboards, lightning, rain
// and snow, the lighting's eye and the old shadows' axis (the terrain's visible sectors and their level of
// detail keep the camera's own position).
void TakeFrameView(const Camera* camera);
const ViewPose& FrameView();
} // namespace camera
