#pragma once

// The camera (CCamera, class id 0x43), the settings record it reads, its saved views, shakes and recorded
// paths, and the engine globals and containers its code uses.

#include "angles.h"
#include "core_streams.h"
#include "core_transform.h"
#include "maths.h"

#include "ei/hook.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include "game_singletons.h"
#include "options.h"

#include <list>
#include <vector>

class CObject3D;

// Zoom distance limits of one camera mode. Inside [min, max] the distance moves freely; between
// max and hardMax (min and hardMin) it eases back; beyond the hard limits it is cut.
struct CameraLimits {
    float min;
    float max;
    float hardMin;
    float hardMax;
};

struct CameraView {
    float distance;
    Quaternion rotation;
    float pitch;
};
static_assert(sizeof(CameraView) == 0x18);

// A shake of the camera (CCamera::shakes_).
struct CameraShake {
    float elapsed;
    float duration;
    float amplitude;
    float decay;
};

// A key of a recorded camera path (camera\*.cam, played by PlayCamera), and a frame of the file
// being recorded (the dword at +4 is never written by the recorder: stack bytes there, zero here).
struct CameraKey {
    float time;
    std::uint32_t reserved; // 0 here (see above)
    Vector3 position;
    Quaternion rotation;
};
static_assert(sizeof(CameraKey) == 0x24);

// Average, lowest and highest surface height on a grid of `step` spacing over
// [p.x - halfX, p.x + halfX] x [p.y - halfY, p.y + halfY] (points off the map skipped; the centre
// counts once more). The two unused pointers are always the zero vector.
void SampleTerrain(const Vector3* p, const Vector3* unused1, const Vector3* unused2, float step,
                             float halfX, float halfY, float* average, float* minimum, float* maximum);

// The camera ("CCamera"). flags_: 0x8 following the target, 0x10 a camera file is open, 0x20 playing a
// camera file, 0x40 free (scripted) placement, 0x100 focus kept inside a circle, 0x200 the alternate limits.
class CCamera final : public CTransformObject {
public:
    CCamera();
    ~CCamera() override;

    int Create() override;
    void Destroy() override;
    int RunTick() override { return 1; }
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    const char* GetClassName() override;
    int GetClassId() override;

    // Starts recording into ".\camera\" + `name` (1 when the file
    // can be made). The other stops recording (the file name emptied); returns 1.
    int OpenFile(const char* name);
    int RecordFrame(std::uint32_t time);

    static constexpr std::uint32_t kFollowing = 0x8;
    static constexpr std::uint32_t kRecording = 0x10;
    static constexpr std::uint32_t kPlaying = 0x20;
    static constexpr std::uint32_t kFree = 0x40;
    static constexpr std::uint32_t kPlaced = kPlaying | kFree;   // placed by its own position, not the focus
    static constexpr std::uint32_t kBounded = 0x100;
    static constexpr std::uint32_t kAlternate = 0x200;

    // Per-frame update: listener, target tracking, pan / turn / tilt / zoom motion, limits, ApplyView, shake.
    void Update(float dt);
    // Places the camera at `distance_` from the focus along the view rotation tilted by
    // the pitch, then lifts it over objects and re-aims at the focus when it was lifted.
    void ApplyView();
    // Moves the focus to `p`, its height set from the terrain around it.
    void SetFocus(const Vector3* p);
    // Pan velocity target from a screen-space direction (x right, y up the screen).
    void Pan(const Vector3* direction);
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
    // Free placement at `from` looking at `to`. Third argument unused.
    void LookFrom(const Vector3* from, const Vector3* to, int unused);
    void SetBounds(const Vector3* center, float radius);
    // Starts a shake from an event at `source` (ignored 20 or more away).
    void Shake(const Vector3* source, float duration, float amplitude, float decay);
    // Recorded camera path: jump to the first key / interpolate at `time` (0 when finished or not playing).
    int PlaybackStart();
    int PlaybackStep(float time);

    // Views and start placement from the settings; the near / far distances and the field of view (floats
    // passed as their dword bit patterns) kept in nearDistance_..fieldOfView_.
    int Init(options::GameOptions* settings, std::uint32_t nearBits, std::uint32_t farBits, std::uint32_t fovBits);

    // Lifts the camera over the terrain and the objects within 16 of it.
    void ClampAboveObjects();
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
    void SetZoomTarget(std::uint32_t value);

    // Follows the object with this id (-1: none); starting to follow resets
    // the two offset vectors to the zero vector.
    void SetTarget(const CEngineObject* target);
    void SetTargetId(int id);

    options::GameOptions* settings_;   // the game options: heights, start placement, limits, saved views
    Vector3 panVelocity_;       // pan velocity
    Vector3 panTarget_;         // pan velocity target
    float panResponse_;
    float turnRate_;
    float turnTarget_;
    float turnResponse_;
    float tiltRate_;
    float tiltTarget_;
    float tiltResponse_;
    float zoomRate_;
    std::uint32_t zoomTarget_;  // the zoom rate target (float bits; SetZoomTarget)
    float zoomResponse_;
    Vector3 focus_;             // the point looked at
    float focusHeight_;         // above the ground
    float distance_;
    Quaternion viewRotation_;   // heading
    float pitch_;               // degrees
    std::list<CameraShake> shakes_;
    Vector3 boundsCenter_;
    float boundsRadius_;
    int targetId_;
    CameraView views_[8];
    float nearDistance_;        // (renderer_init reads the three as zNear / zFar / fov)
    float farDistance_;
    float fieldOfView_;
    std::string fileName_;     // the camera file being recorded
    // Rebirth patches it. Reads the recorded path <data folder>camera\<name> into the keys (as many as the
    // file holds whole). With `settle` above 1 and below the key count, the keys before the first gap of more
    // than 500 time units among the first `settle` take the pose of the key after the gap. Returns 0 when the
    // file does not open, else 1.
    int LoadPath(const char* name, int settle);
    // the recorded path's keys
    std::vector<CameraKey> keys_;
    float timeOffset_;
    int keyIndex_;
};

// The point the camera is placed by: its own position when playing a file or placed freely, else the focus.
const Vector3* CameraAnchor(const CCamera* self);

// Names for the game's globals and constants the camera code uses. The floating-point constants
// are read from the exe at run time (volatile): the game multiplies them at run time and the
// products round at the FPU's precision, which folding at compile time would not.
namespace camera {

inline double D(float f) { return f; }

// What static initialisers leave: pi * 0.5 (the shake's wave runs at this rate), the
// zero vector and the identity rotation the camera copies.
inline constexpr float kHalfPi = /*0079B2A4*/ 1.5707964f;
inline constexpr Vector3 kZeroVector = /*0079B5C8*/ {0.0f, 0.0f, 0.0f};
inline constexpr Quaternion kIdentityRotation = /*0079B5B8*/ {1.0f, 0.0f, 0.0f, 0.0f};

#define CAM_PI 3.141592653589793 /*0073B9A0*/
#define CAM_NINETY 90.0 /*0073BAB8*/
#define CAM_FORTY_FIVE 45.0 /*0073BA28*/
#define CAM_TWO_SEVENTHS 0.2857142857142857 /*0073B9D8*/
#define CAM_ONE_D 1.0 /*0073BAC8*/
#define CAM_ZOOM_STEP 1.0499999523162842 /*0073BAD8*/
#define CAM_LOOK_EPSILON 1e-18 /*0073BAE8*/            // squared length
#define CAM_ALIGN_EPSILON 0.1 /*0073BA90*/
#define CAM_ONE 1.0f /*0073B84C*/
#define CAM_MINUS_ONE (-1.0f /*0073B854*/)
#define CAM_ZERO 0.0f /*0073B858*/
#define CAM_TENTH 0.1f /*0073BA80*/
#define CAM_OBJECT_EXTENT_LIMIT 100.0f /*0073BAB0*/
#define CAM_OBJECT_FADE 10.0f /*0073BAAC*/
#define CAM_DEGREE ei::kRadiansPerDegree                             // one degree in radians (pi * 1/180 at 24 bits)
#define CAM_RIGHT_ANGLE static_cast<float>(CAM_DEGREE * CAM_NINETY)

// The smooth camera (render::SmoothCamera; docs/notes/renderer.md section 14): the main loop
// calls this after each of the client's ticks instead of CCamera::Update. It keeps the position of
// the camera's target after its last two ticks, which Update blends by the client's tickFraction_.
void NoteTargetTick(const CCamera* camera);
} // namespace camera

int CameraCloseFile(CCamera* self);
