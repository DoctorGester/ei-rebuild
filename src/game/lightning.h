#pragma once

// Lightning bolts (00466190-00468A00): CLightning (class id 0x1E, a real virtual class since C28f; game.exe's 0073C550 is retired) and
// the manager that owns and draws them (CLightningManager; a real virtual class: the compiler makes
// its vtable, C19, game.exe's 0073F5B8 is retired). A bolt is a main
// segment from one end point to the other plus six branches that start at points along it; every
// segment is a polyline displaced sideways by a smooth random noise. Names are provisional. See
// docs/notes/objects.md.

#include "base_manager.h"
#include "engine_object.h"
#include "lib/containers.h"
#include "lib/mfc.h"
#include "maths.h"
#include "model.h"   // ClipBounds
#include "game_data.h"

#include <cstddef>
#include <cstdint>

class CTexture;   // texture.h

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

// One polyline point (16 bytes: the fourth float is never written here).
struct LightningPoint {
    float x, y, z, w;
};
static_assert(sizeof(LightningPoint) == 16);

// An aligned array of LightningPoint (the raw operator new block is kept just before the data, at
// data[-1]). Its SetSize is the library instantiation's, reached through
// LightningPointsSetSize (lightning.cpp).
using LightningPointArray = lib::AlignedArray<LightningPoint>;
void __fastcall LightningPointsSetSize(LightningPointArray* points, void*, int newSize, int newGrowBy);

// A random noise key: two rows of the noise table (0..15) and their weights, scaled together to a
// fixed length.
struct NoiseKey {
    std::uint32_t index[2];
    float weight[2];

    // Picks two random rows and two random weights in [-1, 1), then rescales the weights
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

    // Straight segment between two points: start, step and point count (48 below 10
    // units, 72 above 15, 5 per unit in between, a multiple of 4), the displacement axes scaled to
    // the length (at most 15).
    void Init(const Vector3* from, const Vector3* to);
    // Advances the noise keys to `time` (one new key per elapsed unit, at most 100) and
    // writes the two noise curves (one value per point) to outSide / outUp.
    void SampleNoise(float time, float* outSide, float* outUp, float amplitude);
    // Recomputes the points at `time`: the line plus the noise curves, each curve tilted
    // so that it is zero at both ends.
    void Animate(float time, float amplitude);
    // 00468900 (no Ghidra function; lightning_start.cpp): the points empty.
    LightningSegment();
    // 00468920 (lightning_start.cpp): the points' block freed.
    ~LightningSegment();
    LightningSegment(const LightningSegment&) = delete;
    LightningSegment& operator=(const LightningSegment&) = delete;
};

class CLightning final : public CEngineObject {
public:
    // 00468680's inline construction (lightning_start.cpp): the engine object's fields cleared, no
    // textures, the bounds cleared, the segments empty, then reset like slot 3.
    CLightning();
    // 00468810 (deleting: 004687F0; lightning_start.cpp): reset like slot 3, then the segments.
    ~CLightning() override;

    // Slot 2: resets the bolt like slot 3, then adds it to the manager's list of new
    // bolts, which holds a reference until the next DrawAll. Returns 1.
    int Create() override;
    // Slot 3: power 1, amplitude 0.1, releases both textures.
    void Destroy() override;
    const char* GetClassName() override;    // 11: 004687D0 "CLightning"
    int GetClassId() override;              // 12: 004687E0 (0x1E)

    void DrawGlow(void* device, const Vector3* position, int /*frame: not used*/, float size);
    // 00467AD0 (lightning_draw.cpp). Draws the bolt (render device virtuals, glow sprites).
    int Draw(void* device);
    // Grows the bounding box by the points of one segment.
    void ExpandBounds(const LightningSegment* segment);
    // Bounding box and sphere of all segments (centre, box relative to it, radius).
    void UpdateBounds();
    // Moves the branches with the bolt (unless power is 0) and animates all segments.
    void Animate();
    // New end points: main segment, then the branches (spread along the main segment,
    // or random parallel bolts when power is 0); animates when power is negative.
    void SetEndpoints(const Vector3* from, const Vector3* to);
    // 004680D0 (lightning_start.cpp). A new bolt: power, sway, the main segment and six branches
    // (or six parallel bolts for power 0) with fresh noise, animated once, its bounds, and the
    // textures ("Lightning" / "SmtgLight", or the "Demon" pair).
    void Start(const Vector3* from, const Vector3* to, float power, bool demon);

    HeldTexture texture_;               // +0x1C
    HeldTexture glowTexture_;           // +0x20
    // +0x24 the bounds the renderer clips by: flags 3 once set, the box relative to the centre.
    ClipBounds bounds_;
    Vector3 boundsSpare_;               // +0x50 cleared with the bounds, never read
    LightningSegment main_;             // +0x5C
    LightningSegment branches_[6];      // +0x104
    float branchPos_[6];                // +0x4F4 where each branch leaves the main segment (0..1)
    float power_;                       // +0x50C sign selects the look (0: parallel bolts)
    float amplitude_;                   // +0x510 noise amplitude of the branch sway
};

// The noise table, 32 rows of 400 floats (rows 0..15 for the stored keys, 16..31 for the
// key drawn for each sample), read from gfx.res\lightning.dat by LoadLightningNoise.
constexpr int kLightningNoiseRows = 32;
constexpr int kLightningNoiseRowLength = 400;
inline float g_lightningNoise[kLightningNoiseRows][kLightningNoiseRowLength] = /*0079CE78*/ {};
inline float* LightningNoiseTable() { return g_lightningNoise[0]; }
// 00468A00 (cdecl; lightning_load.cpp, A-E1; Rebirth-patched: callers use LoadLightningNoiseOriginal).
// Fills the noise table from the archive; 0 when gfx.res does not open, else 1.
int __cdecl LoadLightningNoise();

// The client world's +0x1A0 (*007AF758). Built by the client world's Setup (world_client_maps.cpp,
// the constructor inlined there), which also holds its lifetime and slots.
class CLightningManager : public CBaseManager {
public:
    // CBaseManager's constructor, then the list of new bolts (vtable 0073F61C, block size 10).
    CLightningManager();
    // Every listed bolt released, the list emptied, then CBaseManager's destructor.
    ~CLightningManager() override;
    const char* GetClassName() override;           // 11: 00562D80 "CLightningManager"
    int GetClassId() override;                     // 12: 00562D90 (0x35)
    // 13: 00562D70, CBaseManager's Init (the same code at its own address).
    // A new bolt.
    CEngineObject* NewObject(int type) override;
    // Draws every bolt it owns (its object list, CBaseManager +0x34), then drops the
    // references of the new-bolt list and empties it. Returns 1.
    int DrawAll(void* device);
    // 004689D0, slot 2 (A-E1; class_slots.cpp). Loads the noise table on the first call (the
    // flag at 007A9694, the loader 00468A00), then the base manager's Create.
    int Create() override;

    mfc::CTypedPtrList<CLightning> newBolts_;   // +0xB0 (each entry holds a reference)
};
static_assert(offsetof(CLightningManager, newBolts_) == 0xB0 && sizeof(CLightningManager) == 0xCC);
