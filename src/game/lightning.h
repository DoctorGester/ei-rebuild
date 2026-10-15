#pragma once

// Lightning bolts (00466190-00468A00): CLightning (class id 0x1E, vtable 0073C550, 0x514 bytes) and
// the manager that owns and draws them (CLightningManager, vtable 0073F5B8). A bolt is a main
// segment from one end point to the other plus six branches that start at points along it; every
// segment is a polyline displaced sideways by a smooth random noise. Names are provisional. See
// docs/notes/objects.md.

#include "base_manager.h"
#include "engine_object.h"
#include "lib/containers.h"
#include "lib/mfc.h"
#include "maths.h"

#include <cstddef>
#include <cstdint>

// One polyline point (16 bytes: the fourth float is never written here).
struct LightningPoint {
    float x, y, z, w;
};
static_assert(sizeof(LightningPoint) == 16);

// An aligned array of LightningPoint (the raw operator new block is kept just before the data, at
// data[-1]). Its SetSize (00468C50) is the library instantiation's, reached through
// LightningPointsSetSize (lightning.cpp).
using LightningPointArray = lib::AlignedArray<LightningPoint>;
void __fastcall LightningPointsSetSize(LightningPointArray* points, void*, int newSize, int newGrowBy);

// A random noise key: two rows of the noise table (0..15) and their weights, scaled together to a
// fixed length.
struct NoiseKey {
    std::uint32_t index[2];
    float weight[2];

    // 00466190. Picks two random rows and two random weights in [-1, 1), then rescales the weights
    // to length `amplitude`.
    void Randomize(float amplitude);
};
static_assert(sizeof(NoiseKey) == 16);

// One segment of a bolt (0xA8 bytes): a straight line from `start` in `points.size` steps of
// `step`, displaced along `side` and `up` by two noise curves.
struct LightningSegment {
    Vector3 start;              // +0x00
    Vector3 side;               // +0x0C displacement axes, both of the bolt's "thickness" length
    Vector3 up;                 // +0x18
    Vector3 step;               // +0x24 (end - start) / point count
    NoiseKey sideKeys[3];       // +0x30 newest first (the noise along `side`)
    NoiseKey upKeys[3];         // +0x60 (the noise along `up`)
    float noiseTime;            // +0x90 time of the newest keys
    LightningPointArray points; // +0x94
    float width;                // +0xA4 passed to the renderer

    // 00466820. Straight segment between two points: start, step and point count (48 below 10
    // units, 72 above 15, 5 per unit in between, a multiple of 4), the displacement axes scaled to
    // the length (at most 15).
    void Init(const Vector3* from, const Vector3* to);
    // 004662C0. Advances the noise keys to `time` (one new key per elapsed unit, at most 100) and
    // writes the two noise curves (one value per point) to outSide / outUp.
    void SampleNoise(float time, float* outSide, float* outUp, float amplitude);
    // 00466CE0. Recomputes the points at `time`: the line plus the noise curves, each curve tilted
    // so that it is zero at both ends.
    void Animate(float time, float amplitude);
    // 00468900 (no Ghidra function; S16a, class_slots.cpp). The constructor (the bolt's vector
    // constructor iterator runs it on the branches): the points empty. Returns this.
    LightningSegment* Construct();
    // 00468920 (A-E1; class_slots.cpp). The destructor (the bolt's vector destructor iterator runs
    // it on the branches): the points' block freed.
    void Destruct();
};
static_assert(offsetof(LightningSegment, sideKeys) == 0x30);
static_assert(offsetof(LightningSegment, noiseTime) == 0x90);
static_assert(offsetof(LightningSegment, points) == 0x94);
static_assert(sizeof(LightningSegment) == 0xA8);

class CLightning : public CEngineObject {
public:
    CLightning* DeleteScalar(unsigned flags);
    void DrawGlow(void* device, const Vector3* position, int /*frame: not used*/, float size);
    // Slot 2 (00466EA0): resets the bolt like slot 3, then adds it to the manager's list of new
    // bolts, which holds a reference until the next DrawAll. Returns 1.
    int Register();
    // Slot 3 (00468790): power 1, amplitude 0.1, releases both textures.
    void Reset();
    // 00467AD0. Draws the bolt (render device virtuals, glow sprites). Not rewritten.
    int Draw(void* device);
    // 00466FD0. Grows the bounding box by the points of one segment.
    void ExpandBounds(const LightningSegment* segment);
    // 00467150. Bounding box and sphere of all segments (centre, box relative to it, radius).
    void UpdateBounds();
    // 004672E0. Moves the branches with the bolt (unless power is 0) and animates all segments.
    void Animate();
    // 00467E10. New end points: main segment, then the branches (spread along the main segment,
    // or random parallel bolts when power is 0); animates when power is negative.
    void SetEndpoints(const Vector3* from, const Vector3* to);
    // 004680D0 (lightning_start.cpp). A new bolt: power, sway, the main segment and six branches
    // (or six parallel bolts for power 0) with fresh noise, animated once, its bounds, and the
    // textures ("Lightning" / "SmtgLight", or the "Demon" pair).
    void Start(const Vector3* from, const Vector3* to, float power, bool demon);

    CEngineObject* texture_;            // +0x1C
    CEngineObject* glowTexture_;        // +0x20
    int boundsKind_;                    // +0x24 3 once the bounds are set (passed to the renderer)
    Vector3 center_;                    // +0x28
    float radius_;                      // +0x34
    Vector3 boundsMin_;                 // +0x38 relative to center_
    Vector3 boundsMax_;                 // +0x44
    Vector3 boundsSpare_;               // +0x50 cleared with the bounds, never read
    LightningSegment main_;             // +0x5C
    LightningSegment branches_[6];      // +0x104
    float branchPos_[6];                // +0x4F4 where each branch leaves the main segment (0..1)
    float power_;                       // +0x50C sign selects the look (0: parallel bolts)
    float amplitude_;                   // +0x510 noise amplitude of the branch sway
};
static_assert(offsetof(CLightning, texture_) == 0x1C);
static_assert(offsetof(CLightning, boundsMin_) == 0x38);
static_assert(offsetof(CLightning, main_) == 0x5C);
static_assert(offsetof(CLightning, branches_) == 0x104);
static_assert(offsetof(CLightning, branchPos_) == 0x4F4);
static_assert(offsetof(CLightning, power_) == 0x50C);
static_assert(sizeof(CLightning) == 0x514);
// 00468810 (lightning_start.cpp). The bolt's destructor body.
void __fastcall LightningDestruct(CLightning* self);

// 0079CE78: the noise table, 32 rows of 400 floats (rows 0..15 for the stored keys, 16..31 for the
// key drawn for each sample), read from gfx.res\lightning.dat by LoadLightningNoise.
constexpr int kLightningNoiseRows = 32;
constexpr int kLightningNoiseRowLength = 400;
inline float* LightningNoiseTable() { return reinterpret_cast<float*>(0x0079CE78); }
// 00468A00 (cdecl; lightning_load.cpp, A-E1; Rebirth-patched: callers use LoadLightningNoiseOriginal).
// Fills the noise table from the archive; 0 when gfx.res does not open, else 1.
int __cdecl LoadLightningNoise();

class CLightningManager : public CBaseManager {
public:
    // 00468940. Draws every bolt it owns (its object list, CBaseManager +0x34), then drops the
    // references of the new-bolt list and empties it. Returns 1.
    int DrawAll(void* device);
    // 004689D0, slot 2 (A-E1; class_slots.cpp). Loads the noise table on the first call (the
    // flag at 007A9694, the loader 00468A00), then the base manager's Create.
    int Create() override;

    mfc::CTypedPtrList<CLightning> newBolts_;   // +0xB0 (each entry holds a reference)
};
static_assert(offsetof(CLightningManager, newBolts_) == 0xB0);
