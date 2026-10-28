#pragma once

// Lightning bolts: CLightning (class id 0x1E) and the manager that owns and draws them (CLightningManager). A
// bolt is a main segment from one end point to the other plus six branches that start at points along it;
// every segment is a polyline displaced sideways by a smooth random noise. Names are provisional. See
// docs/notes/objects.md.

#include "base_manager.h"
#include "engine_object.h"
#include "lib/containers.h"
#include <cstdint>
#include "maths.h"
#include "model.h"   // ClipBounds

#include <cstddef>
#include <list>
#include <vector>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

// One polyline point (16 bytes: the fourth float is never written here).
struct LightningPoint {
    float x, y, z, w;
};

// A random noise key: two rows of the noise table (0..15) and their weights, scaled together to a
// fixed length.
struct NoiseKey {
    std::uint32_t index[2];
    float weight[2];

    // Picks two random rows and two random weights in [-1, 1), then rescales the weights
    // to length `amplitude`.
    void Randomize(float amplitude);
};

// One segment of a bolt: a straight line from `start` in `points.size` steps of
// `step`, displaced along `side` and `up` by two noise curves.
struct LightningSegment {
    Vector3 start;
    Vector3 side;               // displacement axes, both of the bolt's "thickness" length
    Vector3 up;
    Vector3 step;               // (end - start) / point count
    NoiseKey sideKeys[3];       // newest first (the noise along `side`)
    NoiseKey upKeys[3];         // (the noise along `up`)
    float noiseTime;            // time of the newest keys
    std::vector<LightningPoint> points;
    float width;                // passed to the renderer

    // Straight segment between two points: start, step and point count (48 below 10 units, 72 above 15, 5 per
    // unit in between, a multiple of 4), the displacement axes scaled to the length (at most 15).
    void Init(const Vector3* from, const Vector3* to);
    // Advances the noise keys to `time` (one new key per elapsed unit, at most 100) and
    // writes the two noise curves (one value per point) to outSide / outUp.
    void SampleNoise(float time, float* outSide, float* outUp, float amplitude);
    // Recomputes the points at `time`: the line plus the noise curves, each curve tilted
    // so that it is zero at both ends.
    void Animate(float time, float amplitude);
    LightningSegment();
    ~LightningSegment();
    LightningSegment(const LightningSegment&) = delete;
    LightningSegment& operator=(const LightningSegment&) = delete;
};

class CLightning final : public CEngineObject {
public:
    CLightning();
    ~CLightning() override;

    // Resets the bolt like Destroy, then adds it to the manager's list of new bolts, which holds a
    // reference until the next DrawAll. Returns 1.
    int Create() override;
    // Power 1, amplitude 0.1, releases both textures.
    void Destroy() override;
    const char* GetClassName() override;
    int GetClassId() override;

    void DrawGlow(CSceneRenderer* device, const Vector3* position, int /*frame: not used*/, float size);
    // Draws the bolt (render device virtuals, glow sprites); lightning_draw.cpp.
    int Draw(CSceneRenderer* device);
    // Grows the bounding box by the points of one segment.
    void ExpandBounds(const LightningSegment* segment);
    // Bounding box and sphere of all segments (centre, box relative to it, radius).
    void UpdateBounds();
    // Moves the branches with the bolt (unless power is 0) and animates all segments.
    void Animate();
    // New end points: main segment, then the branches (spread along the main segment, or random
    // parallel bolts when power is 0); animates when power is negative.
    void SetEndpoints(const Vector3* from, const Vector3* to);
    // A new bolt (lightning_start.cpp): power, sway, the main segment and six branches (or six parallel
    // bolts for power 0) with fresh noise, animated once, its bounds, and the textures ("Lightning" /
    // "SmtgLight", or the "Demon" pair).
    void Start(const Vector3* from, const Vector3* to, float power, bool demon);

    HeldTexture texture_;
    HeldTexture glowTexture_;
    // The bounds the renderer clips by: flags 3 once set, the box relative to the centre.
    ClipBounds bounds_;
    Vector3 boundsSpare_;               // cleared with the bounds, never read
    LightningSegment main_;
    LightningSegment branches_[6];
    float branchPos_[6];                // where each branch leaves the main segment (0..1)
    float power_;                       // sign selects the look (0: parallel bolts)
    float amplitude_;                   // noise amplitude of the branch sway
};

// The noise table, 32 rows of 400 floats (rows 0..15 for the stored keys, 16..31 for the
// key drawn for each sample), read from gfx.res\lightning.dat by LoadLightningNoise.
constexpr int kLightningNoiseRows = 32;
constexpr int kLightningNoiseRowLength = 400;
inline float g_lightningNoise[kLightningNoiseRows][kLightningNoiseRowLength] = /*0079CE78*/ {};
// (lightning_load.cpp; Rebirth-patched: callers use LoadLightningNoiseOriginal).
// Fills the noise table from the archive; 0 when gfx.res does not open, else 1.
int LoadLightningNoise();

// The client world's lightning manager. Built by the client world's Setup (world_client_maps.cpp,
// the constructor inlined there), which also holds its lifetime and slots.
class CLightningManager : public CBaseManager {
public:
    CLightningManager();
    ~CLightningManager() override;
    const char* GetClassName() override;
    int GetClassId() override;
    // A new bolt.
    CEngineObject* NewObject(int type) override;
    // Draws every bolt it owns (its object list), then drops the references of the new-bolt list and
    // empties it. Returns 1.
    int DrawAll(CSceneRenderer* device);
    // Create (class_slots.cpp): loads the noise table on the first call, then the base manager's Create.
    int Create() override;

    std::list<CLightning*> newBolts_;   // (each entry holds a reference)
};
