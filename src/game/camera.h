#pragma once

// The camera (CCamera, class id 0x43, vtable 0073B9E0), the settings record it reads, its saved
// views, shakes and recorded paths, and the engine globals and containers its code uses.
// camera.cpp: update, view placement, focus, zoom, saved views, shakes, recorded-path playback.
// camera_motion.cpp: aiming, turning upright, keeping above objects. camera_object.cpp: the
// object's construction, vtable slots, camera files and saving. Names are provisional.

#include "angles.h"
#include "core_streams.h"
#include "core_transform.h"
#include "maths.h"

#include "ei/hook.h"
#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

class CBaseManager;
class CObject3D;
class Terrain;

// Zoom distance limits of one camera mode. Inside [min, max] the distance moves freely; between
// max and hardMax (min and hardMin) it eases back; beyond the hard limits it is cut.
struct CameraLimits {
    float min;      // +0x0
    float max;      // +0x4
    float hardMin;  // +0x8
    float hardMax;  // +0xC
};
static_assert(sizeof(CameraLimits) == 0x10);

// One of the four saved views as the settings record keeps it: rotation as axis and angle.
struct CameraViewRecord {
    float distance;     // +0x00
    Vector3 axis;       // +0x04
    float angle;        // +0x10
    float pitch;        // +0x14
};
static_assert(sizeof(CameraViewRecord) == 0x18);

// The camera settings object (camera +0x94). Only the fields the camera code reads.
struct CameraSettings {
    std::uint8_t unused_000[0xFC];
    float clearance;            // +0xFC  height kept above objects under the camera
    float maxRaise;             // +0x100
    float startFocusZ;          // +0x104 the focus' start height (Init)
    float startFocusX;          // +0x108
    float startFocusY;          // +0x10C
    CameraLimits normal;        // +0x110
    float startDistance;        // +0x120 (Init: also saved views 6 and 7)
    CameraLimits alternate;     // +0x124 (camera flag 0x200)
    float altPitchMin;          // +0x134 degrees
    float altPitchMax;          // +0x138
    std::uint8_t unused_13C[0x178 - 0x13C];
    CameraViewRecord views[4];  // +0x178
    float pitchMin;             // +0x1D8 degrees
    float pitchMax;             // +0x1DC
};
static_assert(offsetof(CameraSettings, startFocusZ) == 0x104);
static_assert(offsetof(CameraSettings, normal) == 0x110);
static_assert(offsetof(CameraSettings, startDistance) == 0x120);
static_assert(offsetof(CameraSettings, alternate) == 0x124);
static_assert(offsetof(CameraSettings, views) == 0x178);
static_assert(offsetof(CameraSettings, pitchMax) == 0x1DC);

// A saved view (camera +0x120, eight of them; keys in 005EADD0).
struct CameraView {
    float distance;         // +0x00
    Quaternion rotation;    // +0x04
    float pitch;            // +0x14
};
static_assert(sizeof(CameraView) == 0x18);

// A shake of the camera (CCamera::shakes_, an STLport list<CameraShake>: 24-byte nodes from the
// node allocator; constructor 00411660, end() 00411530, insert 00411630, erase 00411540).
struct CameraShake {
    float elapsed;      // +0x00
    float duration;     // +0x04
    float amplitude;    // +0x08
    float decay;        // +0x0C
};
using ShakeList = stlp::list<CameraShake>;
static_assert(sizeof(ShakeList::Node) == 0x18);

// A key of a recorded camera path (camera\*.cam, played by PlayCamera), and a frame of the file
// being recorded (the dword at +4 is never written by the recorder: stack bytes there, zero here).
struct CameraKey {
    float time;             // +0x00
    std::uint32_t reserved; // +0x04 0 here (see above)
    Vector3 position;       // +0x08
    Quaternion rotation;    // +0x14
};
static_assert(sizeof(CameraKey) == 0x24);

// A cubic curve in 3D: polynomial coefficients c0..c3 for x, then y, then z (as CubicSegment).
struct CubicCurve3 {
    float c[12];
    // 0040E460. Point at t; returns out.
    Vector3* Evaluate(Vector3* out, float t) const;
};

// The deque of saved view stack entries (0x24-byte elements, 14 per node) the camera owns at
// +0x1E0: constructor 00411AB0, destructor 0040CFB0 (its _Destroy, 00411DF0, does nothing).
using ViewStack = stlp::deque<stlp::Opaque<0x24>>;
static_assert(sizeof(ViewStack) == 0x28);
static_assert(sizeof(ViewStack) == 0x28);

// 0040D9B0. Average, lowest and highest surface height on a grid of `step` spacing over
// [p.x - halfX, p.x + halfX] x [p.y - halfY, p.y + halfY] (points off the map skipped; the centre
// counts once more). The two unused pointers are always the zero vector.
void __stdcall SampleTerrain(const Vector3* p, const Vector3* unused1, const Vector3* unused2, float step,
                             float halfX, float halfY, float* average, float* minimum, float* maximum);

// The camera ("CCamera", vtable 0073B9E0). Flags (+0x08): 0x8 following the target, 0x10 a camera
// file is open, 0x20 playing a camera file, 0x40 free (scripted) placement, 0x100 focus kept
// inside a circle, 0x200 the alternate limits.
class CCamera : public CTransformObject {
public:
    CCamera* DeleteScalar(unsigned flags);
    void GetRange(std::uint32_t* a, std::uint32_t* b, std::uint32_t* c) const;
    // camera_object.cpp. 0040E910 (RET 4): starts recording into ".\camera\" + `name` (1 when the file
    // can be made). 0040ECB0: stops recording (the file name emptied); returns 1.
    int OpenFile(const char* name);
    int RecordFrame(std::uint32_t time);
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073B9E0);

    static constexpr std::uint32_t kFollowing = 0x8;
    static constexpr std::uint32_t kRecording = 0x10;
    static constexpr std::uint32_t kPlaying = 0x20;
    static constexpr std::uint32_t kFree = 0x40;
    static constexpr std::uint32_t kPlaced = kPlaying | kFree;   // placed by its own position, not the focus
    static constexpr std::uint32_t kBounded = 0x100;
    static constexpr std::uint32_t kAlternate = 0x200;

    // camera.cpp
    // 0040FB30. Per-frame update: listener, target tracking, pan / turn / tilt / zoom motion,
    // limits, ApplyView, shake.
    void Update(float dt);
    // 0040F8B0. Places the camera at `distance_` from the focus along the view rotation tilted by
    // the pitch, then lifts it over objects and re-aims at the focus when it was lifted.
    void ApplyView();
    // 0040F5C0. Moves the focus to `p`, its height set from the terrain around it.
    void SetFocus(const Vector3* p);
    // 0040F230. Pan velocity target from a screen-space direction (x right, y up the screen).
    void Pan(const Vector3* direction);
    // 0040F430. Stops panning, turning, tilting and zooming.
    void StopMotion();
    // 004107A0. Zoom by 1.05^steps, clamped to the current limits.
    void ZoomBy(float steps);
    // 00410860 / 004108D0. Saved views 0-7 (false for other slots). Restore sets distance and
    // pitch only.
    bool SaveView(unsigned slot);
    bool RestoreView(unsigned slot);
    // 00411470. Sets distance and pitch from a view; stops all motion.
    void SetDistancePitch(const CameraView* view);
    // 00410D70 / 00410D10. Views 0-3 to / from the settings record.
    void ViewsToSettings(CameraSettings* settings);
    void ViewsFromSettings(const CameraSettings* settings);
    // 004109E0. View rotation = `angle` radians about Z, then ApplyView.
    void SetHeading(float angle);
    // 00410E50. Free placement on / off.
    void SetFree(bool on);
    // 00411060. Free placement at `from` looking at `to`. Third argument unused.
    void LookFrom(const Vector3* from, const Vector3* to, int unused);
    // 00411360. Keep the focus within `radius` of `center`.
    void SetBounds(const Vector3* center, float radius);
    // 004113A0. Starts a shake from an event at `source` (ignored 20 or more away).
    void Shake(const Vector3* source, float duration, float amplitude, float decay);
    // 0040F1B0 / 0040EFB0. Recorded camera path: jump to the first key / interpolate at `time`
    // (0 when finished or not playing).
    int PlaybackStart();
    int PlaybackStep(float time);

    // camera_object.cpp
    CCamera* Construct();                   // 0040C8B0: constructor body; returns this
    void Destruct();                        // 0040CE60: destructor body
    // 0040D0A0. Views and start placement from the settings; the near / far distances and the
    // field of view (floats passed as their dword bit patterns) kept at +0x2E0..+0x2E8.
    int Init(CameraSettings* settings, std::uint32_t nearBits, std::uint32_t farBits, std::uint32_t fovBits);
    void LoadState(CStream* stream);        // 00410AB0
    void SaveState(CStream* stream);        // 00410C10

    // camera_motion.cpp
    // 0040E4C0. Lifts the camera over the terrain and the objects within 16 of it.
    void ClampAboveObjects();
    // 0040D710. Turns the camera a tenth of the way upright, then ClampAboveObjects.
    void AlignToTarget();
    // 004110D0. Aims along the direction; false (nothing done) for a zero-length one.
    bool LookDirection(float dx, float dy, float dz);

    // core_transform.cpp
    // 0040E810. Where the camera's view axis (rotated Z) meets the ground plane z = 0; the camera
    // position itself when the axis is (nearly) horizontal or points away from the plane.
    void GroundPoint(Vector3* out) const;

    // 0040F360 / 0040F3C0. Set a target rate; the current rate jumps up to it when larger in
    // magnitude. Names provisional (driven by mouse rotation in 005EAD50).
    void SetTurnRate(float rate);
    void SetTiltRate(float rate);
    // 0040F420.
    void SetZoomTarget(std::uint32_t value);

    // 0040D3B0 / 0040D420. Follows the object with this id (-1: none); starting to follow resets
    // the two offset vectors to the zero vector.
    void SetTarget(const CEngineObject* target);
    void SetTargetId(int id);

    std::uint8_t unused_54[0x94 - 0x54];
    CameraSettings* settings_;  // +0x94
    Vector3 offset98_;          // +0x98 pan velocity
    Vector3 offsetA4_;          // +0xA4 pan velocity target
    float panResponse_;         // +0xB0
    float turnRate_;            // +0xB4
    float turnTarget_;          // +0xB8
    float turnResponse_;        // +0xBC
    float tiltRate_;            // +0xC0
    float tiltTarget_;          // +0xC4
    float tiltResponse_;        // +0xC8
    float zoomRate_;            // +0xCC
    std::uint32_t zoomTarget_;  // +0xD0 the zoom rate target (float bits; SetZoomTarget)
    float zoomResponse_;        // +0xD4
    std::uint8_t unused_D8[0xE0 - 0xD8];
    Vector3 focus_;             // +0xE0 the point looked at
    float focusHeight_;         // +0xEC above the ground
    float distance_;            // +0xF0
    Quaternion viewRotation_;   // +0xF4 heading
    float pitch_;               // +0x104 degrees
    ShakeList shakes_;          // +0x108
    Vector3 boundsCenter_;      // +0x10C
    float boundsRadius_;        // +0x118
    int targetId_;              // +0x11C
    CameraView views_[8];       // +0x120
    ViewStack viewStack_;       // +0x1E0
    std::uint8_t unused_208[0x2E0 - 0x208];
    float nearDistance_;        // +0x2E0 (renderer_init reads the three as zNear / zFar / fov)
    float farDistance_;         // +0x2E4
    float fieldOfView_;         // +0x2E8
    mfc::CString fileName_;     // +0x2EC the camera file being recorded
    // 0040ECD0 (RET 8; camera_path_load.cpp, A-E1; Rebirth-patched, so the vanilla launch only and
    // callers use CameraLoadPathOriginal). Reads the recorded path <data folder>camera\<name> into
    // the keys (as many as the file holds whole). With `settle` above 1 and below the key count,
    // the keys before the first gap of more than 500 time units among the first `settle` take
    // the pose of the key after the gap. Returns 0 when the file does not open, else 1.
    int LoadPath(const char* name, int settle);
    // The keys as the engine's aligned array they are (+0x2F0..+0x2FC: data, size, capacity,
    // grow step).
    lib::AlignedArray<CameraKey>* KeyArray() { return reinterpret_cast<lib::AlignedArray<CameraKey>*>(&keys_); }

    CameraKey* keys_;           // +0x2F0 (the block start is kept in the dword before the keys)
    int keyCount_;              // +0x2F4
    std::uint32_t keyCapacity_; // +0x2F8 the keys' capacity
    std::uint32_t keyGrowBy_;   // +0x2FC the keys' grow step (0: an eighth of the size, 4..1024)
    float timeOffset_;          // +0x300
    int keyIndex_;              // +0x304
};
static_assert(offsetof(CCamera, settings_) == 0x94);
static_assert(offsetof(CCamera, offset98_) == 0x98);
static_assert(offsetof(CCamera, turnRate_) == 0xB4);
static_assert(offsetof(CCamera, tiltTarget_) == 0xC4);
static_assert(offsetof(CCamera, zoomTarget_) == 0xD0);
static_assert(offsetof(CCamera, focus_) == 0xE0);
static_assert(offsetof(CCamera, viewRotation_) == 0xF4);
static_assert(offsetof(CCamera, shakes_) == 0x108);
static_assert(offsetof(CCamera, targetId_) == 0x11C);
static_assert(offsetof(CCamera, views_) == 0x120);
static_assert(offsetof(CCamera, viewStack_) == 0x1E0);
static_assert(offsetof(CCamera, nearDistance_) == 0x2E0);
static_assert(offsetof(CCamera, fileName_) == 0x2EC);
static_assert(offsetof(CCamera, keys_) == 0x2F0);
static_assert(offsetof(CCamera, keyIndex_) == 0x304);

// The STLport list<CObject3D*> (12-byte nodes) that CWorldClient::ObjectsInRange (0056DAF0) fills.
using NearbyList = stlp::list<CObject3D*>;
using NearbyNode = NearbyList::Node;

// 0040FB20 (camera_object.cpp): the point the camera is placed by: its own position when playing a
// file or placed freely, else the focus.
const Vector3* __fastcall CameraAnchor(const CCamera* self);

// Names for the game's globals and constants the camera code uses. The floating-point constants
// are read from the exe at run time (volatile): the original multiplies them at run time and the
// products round at the FPU's precision, which folding at compile time would not.
namespace camera {

inline double D(float f) { return f; }

inline float GameFloat(std::uintptr_t address) { return ei::GameData<const volatile float>(address); }

inline Terrain* TheTerrain() { return ei::GameData<Terrain* const>(0x007AF738); }   // width / height
inline float CellSize() { return ei::GameData<const float>(0x0079BDB4); }
inline float ClockFloat() { return ei::GameData<const float>(0x0079B2A4); }
inline const Vector3& ZeroVector() { return ei::GameData<const Vector3>(0x0079B5C8); }
inline const Quaternion& IdentityRotation() { return ei::GameData<const Quaternion>(0x0079B5B8); }
inline void*& NodeFreeList() { return ei::GameData<void*>(0x0079B228); }   // free list of the 12-byte nodes
inline std::uint32_t& GlobalFlags() { return ei::GameData<std::uint32_t>(0x007AF5C0); }   // bit 0x10: camera file made

// Constants (addresses in the exe in the comments).
#define CAM_PI 3.141592653589793 /*0073B9A0*/
#define CAM_NINETY 90.0 /*0073BAB8*/                  // 90
#define CAM_FORTY_FIVE 45.0 /*0073BA28*/              // 45
#define CAM_TWO_SEVENTHS 0.2857142857142857 /*0073B9D8*/            // 2 / 7
#define CAM_ONE_D 1.0 /*0073BAC8*/                   // 1.0
#define CAM_ZOOM_STEP 1.0499999523162842 /*0073BAD8*/               // 1.05
#define CAM_LOOK_EPSILON 1e-18 /*0073BAE8*/            // 1e-18 (squared length)
#define CAM_ALIGN_EPSILON 0.1 /*0073BA90*/           // 0.1
#define CAM_ONE 1.0f /*0073B84C*/                      // 1.0f
#define CAM_MINUS_ONE (-1.0f /*0073B854*/)                // -1.0f
#define CAM_ZERO 0.0f /*0073B858*/                     // 0.0f
#define CAM_TENTH 0.1f /*0073BA80*/                    // 0.1f
#define CAM_OBJECT_EXTENT_LIMIT 100.0f /*0073BAB0*/      // 100.0f
#define CAM_OBJECT_FADE 10.0f /*0073BAAC*/              // 10.0f
#define CAM_DEGREE ei::kRadiansPerDegree                             // one degree in radians (pi * 1/180 at 24 bits)
#define CAM_RIGHT_ANGLE static_cast<float>(CAM_DEGREE * CAM_NINETY)

} // namespace camera

int __fastcall CameraCloseFile(CCamera* self);
