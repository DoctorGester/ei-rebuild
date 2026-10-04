#pragma once

#include "engine_object.h"
#include "maths.h"

#include <cstddef>
#include <cstdint>

// An engine object placed in the world: local position and rotation relative to its parent, and
// the world transform derived from them. Base of the camera, the scene's figures, units and other
// placed objects (abstract: slots 11 and 12 are pure). Name provisional; vtable 0073BA38, 18 slots.
// Slots 0-12 are CEngineObject's (CopyFrom 00402D40, Release 00455980, empty Load/Save).
class CTransformObject : public CEngineObject {
public:
    // 13: 00401490. Copies the local and world transform; returns this.
    virtual CTransformObject* CopyTransform(const CTransformObject& other);
    // 14: 0040CB80. worldRotation = parent * localRotation. Overridden by 00516370 in units.
    virtual void CombineRotation(const Quaternion& parent);
    // 15: 0040CC30. Recomputes the world transform from the parent's rotation and position.
    virtual void UpdateWorld(const Quaternion& parentRotation, const Vector3& parentPosition);
    // 16: 00401510. Writes the local transform to an MFC CFile (slot 14, Write).
    virtual void SaveTransform(void* file);
    // 17: 00401540. Reads the local transform from an MFC CFile (slot 13, Read).
    virtual void LoadTransform(void* file);

    // 00453400 (object.cpp). Constructor body: base fields, identity rotations (positions left as
    // they are), vtable 0073BA38; returns this.
    CTransformObject* ConstructBase();

    // Rotation helpers (core_transform_math.cpp).
    void WorldMatrix(float* m) const;                    // 00401570: 4x4 world matrix
    void TiltTowards(const Vector3& target);             // 00401680: down axis a tenth towards target
    void TurnTowards(const Vector3& target);             // 00401940: forward axis onto target
    void RollTowards(const Vector3& dir);                // 00401B50: down axis towards dir about forward
    void Rotate(const Vector3& axis, float angle);       // 00401E50: localRotation = turn * localRotation
    void TurnUpTowards(const Vector3& dir, int reduced); // 00401F00: up axis towards dir (0.4 of it if reduced)
    void SetHeading(const float* dir);                   // 00402220: turn about +Z by atan2(dir[1], dir[0])

    Vector3 localPosition_;     // +0x1C
    Vector3 worldPosition_;     // +0x28
    Quaternion localRotation_;  // +0x34
    Quaternion worldRotation_;  // +0x44
};
static_assert(offsetof(CTransformObject, localPosition_) == 0x1C);
static_assert(offsetof(CTransformObject, localRotation_) == 0x34);
static_assert(sizeof(CTransformObject) == 0x54);

// Zoom distance limits of one camera mode (camera.cpp). Inside [min, max] the distance moves
// freely; between max and hardMax (min and hardMin) it eases back; beyond the hard limits it is cut.
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
    std::uint8_t unknown_000[0xFC];
    float clearance;            // +0xFC  height kept above objects under the camera
    float maxRaise;             // +0x100
    std::uint8_t unknown_104[0x110 - 0x104];
    CameraLimits normal;        // +0x110
    std::uint8_t unknown_120[0x124 - 0x120];
    CameraLimits alternate;     // +0x124 (camera flag 0x200)
    float altPitchMin;          // +0x134 degrees
    float altPitchMax;          // +0x138
    std::uint8_t unknown_13C[0x178 - 0x13C];
    CameraViewRecord views[4];  // +0x178
    float pitchMin;             // +0x1D8 degrees
    float pitchMax;             // +0x1DC
};
static_assert(offsetof(CameraSettings, normal) == 0x110);
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

// A node of the camera's shake list (an STLport list on the game's node allocator).
struct CameraShake {
    CameraShake* next;
    CameraShake* prev;
    float elapsed;      // +0x08
    float duration;     // +0x0C
    float amplitude;    // +0x10
    float decay;        // +0x14
};
static_assert(sizeof(CameraShake) == 0x18);

// A key of a recorded camera path (camera\*.cam, played by PlayCamera).
struct CameraKey {
    float time;             // +0x00
    std::uint32_t unknown_04;
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

// 0040D9B0. Average, lowest and highest surface height on a grid of `step` spacing over
// [p.x - halfX, p.x + halfX] x [p.y - halfY, p.y + halfY] (points off the map skipped; the centre
// counts once more). The two unused pointers are always the zero vector.
void __stdcall SampleTerrain(const Vector3* p, const Vector3* unused1, const Vector3* unused2, float step,
                             float halfX, float halfY, float* average, float* minimum, float* maximum);

// The camera ("CCamera", class id 0x43, vtable 0073B9E0). Flags (+0x08): 0x8 following the target,
// 0x10 a camera file is open, 0x20 playing a camera file, 0x40 free (scripted) placement, 0x100
// focus kept inside a circle, 0x200 the alternate limits. Names provisional.
class CCamera : public CTransformObject {
public:
    static constexpr std::uint32_t kFollowing = 0x8;
    static constexpr std::uint32_t kPlaying = 0x20;
    static constexpr std::uint32_t kFree = 0x40;
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
    void Pan(const float* direction);
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
    // 0040D0A0. Views and start placement from the settings; a / b / c kept at +0x2E0..+0x2E8.
    int Init(CameraSettings* settings, std::uint32_t a, std::uint32_t b, std::uint32_t c);
    void LoadState(void* stream);           // 00410AB0 (WorldStream: slot 1 Read, +8 version)
    void SaveState(void* stream);           // 00410C10 (slot 2 Write)

    // camera_motion.cpp
    // 0040E4C0. Lifts the camera over the terrain and the objects within 16 of it.
    void ClampAboveObjects();
    // 0040D710. Turns the camera a tenth of the way upright, then ClampAboveObjects.
    void AlignToTarget();
    // 004110D0. Aims along the direction; false (nothing done) for a zero-length one.
    bool LookDirection(float dx, float dy, float dz);


    // 0040E810. Where the camera's view axis (rotated Z) meets the ground plane z = 0; the camera
    // position itself when the axis is (nearly) horizontal or points away from the plane.
    void GroundPoint(Vector3* out) const;

    // 0040F360 / 0040F3C0. Set a target rate; the current rate jumps up to it when larger in
    // magnitude. Names provisional (driven by mouse rotation in 005EAD50).
    void SetTurnRate(float rate);
    void SetTiltRate(float rate);
    // 0040F420.
    void SetUnknownD0(std::uint32_t value);

    // 0040D3B0 / 0040D420. Follows the object with this id (-1: none); starting to follow resets
    // the two offset vectors to the zero vector.
    void SetTarget(const CEngineObject* target);
    void SetTargetId(int id);

    std::uint8_t unknown_54[0x94 - 0x54];
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
    std::uint32_t unknownD0_;   // +0xD0 zoom rate target (float)
    float zoomResponse_;        // +0xD4
    std::uint8_t unknown_D8[0xE0 - 0xD8];
    Vector3 focus_;             // +0xE0 the point looked at
    float focusHeight_;         // +0xEC above the ground
    float distance_;            // +0xF0
    Quaternion viewRotation_;   // +0xF4 heading
    float pitch_;               // +0x104 degrees
    CameraShake* shakes_;       // +0x108 list sentinel
    Vector3 boundsCenter_;      // +0x10C
    float boundsRadius_;        // +0x118
    int targetId_;              // +0x11C
    CameraView views_[8];       // +0x120
    std::uint8_t unknown_1E0[0x2F0 - 0x1E0];   // view stack (deque), file name at +0x2EC
    CameraKey* keys_;           // +0x2F0
    int keyCount_;              // +0x2F4
    std::uint8_t unknown_2F8[0x300 - 0x2F8];
    float timeOffset_;          // +0x300
    int keyIndex_;              // +0x304
};
static_assert(offsetof(CCamera, settings_) == 0x94);
static_assert(offsetof(CCamera, offset98_) == 0x98);
static_assert(offsetof(CCamera, turnRate_) == 0xB4);
static_assert(offsetof(CCamera, tiltTarget_) == 0xC4);
static_assert(offsetof(CCamera, unknownD0_) == 0xD0);
static_assert(offsetof(CCamera, focus_) == 0xE0);
static_assert(offsetof(CCamera, viewRotation_) == 0xF4);
static_assert(offsetof(CCamera, shakes_) == 0x108);
static_assert(offsetof(CCamera, targetId_) == 0x11C);
static_assert(offsetof(CCamera, views_) == 0x120);
static_assert(offsetof(CCamera, keys_) == 0x2F0);
static_assert(offsetof(CCamera, keyIndex_) == 0x304);
