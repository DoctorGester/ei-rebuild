#pragma once

// Terrain sectors: the 32x32-cell blocks of the landscape, loaded on demand from the map's resource archive
// (<zone>XXXYYY.sec) and turned into per-vertex records and per-texture tile lists. A sector has 33x33
// vertices per layer (ground, and water when the sector has water) and 16x16 tiles of 2x2 cells. The tiles
// are grouped into four 8x8-tile quarters; each quarter keeps, per texture page, a byte array of tile edge
// flags and a list of tile entries (3x3 vertices each).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "model.h"     // ClipBounds
#include "terrain.h"

#include "lib/vecmath.h"

#include "lib/containers.h"
#include "lib/mfc.h"

class CLight;
class CTransformPipeline;

namespace terrain {

struct LightRefBlock;

// One entry per lit vertex: the point lights that reach it (pooled 16-byte nodes).
struct LightRef {
    LightRef* next;
    LightRef* prev;
    LightRefBlock* block;   // the pool block it was taken from
    CLight* light;
};

// The LightRef pool: blocks of 1000 nodes from operator new. Blocks with free nodes
// are listed in one doubly linked list, full ones in another (newest first); a block whose nodes are all
// free again is deleted.
struct LightRefBlock {
    static constexpr int kRefs = 1000;
    int freeCount;
    LightRefBlock* next;
    LightRefBlock* prev;
    LightRef* freeList;
    LightRef refs[kRefs];
};
static_assert(sizeof(LightRefBlock) == 0x3E90);

// A node from the pool; its next / prev / light are the caller's to set.
LightRef* AllocLightRef();
// Returns `ref` to its block.
void __stdcall FreeLightRef(LightRef* ref);

#pragma pack(push, 1)
// Per-vertex record (packed).
struct SectorVertex {
    Vector4 projected;                  // screen position (Project)
    std::uint8_t outcode;               // clip flags when the quarter crosses the view, else 0
    std::uint32_t colour;               // A (materialByte) R G B, the lighting stage
    std::uint32_t specular;             // edge fade << 24, then the specular lights' R G B
    Vector3 position;                   // water layer: the vertex position
    std::uint16_t waterMaterial;        // water layer: material index, 0xFFFF none
    float depth;                        // ground layer: water z - ground z
    Vector3 normal;
    Vector3 lit;                        // ambient and sun light
    Vector3 materialColour;             // the water material's colour
    std::uint8_t materialByte;          // the water material's opacityByte, 0xFF none
    LightRef* lights;

    // Returns every node of `lights` to the pool.
    void ReleaseLights();
};
#pragma pack(pop)
static_assert(sizeof(SectorVertex) == 0x54);

// A tile entry vertex: the record it uses and its texture coordinates.
struct TileVertex {
    SectorVertex* vertex;
    float u;
    float v;
};
static_assert(sizeof(TileVertex) == 0xC);

// An animated tile (texture with several frames).
struct AnimatedTile {
    TileVertex* vertices;   // the tile's 9 entries
    int rotation;
    int frameBase;          // index of the texture within its page
    int frame;              // random start frame
    int frames;
    int speed;              // from the speed table by texture class
};
static_assert(sizeof(AnimatedTile) == 0x18);

struct SectorBox : ClipBounds {
    std::uint32_t unused_2C[3];
};
static_assert(sizeof(SectorBox) == 0x38);
inline const ClipBounds* BoundsOf(const SectorBox& box) { return &box; }

// CTransformPipeline::ClipTest's verdicts as the sector keeps them (clipMode, quarterMode): 0 inside the
// view, 1 crossing its edge, 2 outside (-1 is stored as 2).
constexpr int kCrossing = 1;
constexpr int kCulled = 2;

struct SectorPosition : Vector3 {};
static_assert(sizeof(SectorPosition) == 12);

void __stdcall DestroyRecords(SectorVertex* records, int count);

// `light` is removed from a vertex's list when `remove` is set or the vertex is not nearer than its range to
// `position`; otherwise it is added at the list's head when missing.
void __stdcall UpdateVertexLights(SectorData* sector, CLight* light, const Vector3* position, const LightArea* area,
                                  int remove);

struct RecordElements {
    static void Construct(SectorVertex* records, int count);
    static void Destruct(SectorVertex* records, int count);
};
using FlagArray = mfc::CArray<std::uint8_t>;
struct FlagArrayElements {
    static void Construct(FlagArray* arrays, int count);
    static void Destruct(FlagArray* arrays, int count);
};
using TileList = std::vector<TileVertex>;
struct TileListElements {
    static void Construct(TileList* lists, int count);
    static void Destruct(TileList* lists, int count);
    static void Relocate(TileList* to, TileList* from, int count);
};

struct SectorData {
    int DrawPage(void* context, void* /*unused*/, int page, int /*unused*/);
    std::uint8_t hasSpecular;           // a specular light reached a vertex (Light clears it)
    std::uint8_t pad_001[3];
    std::uint32_t built;                // 1 loaded, 2 geometry, 4 lit
    int sx, sy;
    int hasWater;
    int hasWaterTiles;                  // a second tile layer (water tiles)
    const std::uint8_t* data;           // the .sec file after its 5-byte header
    Terrain* owner;
    int pages;                          // the owner's
    int perPage;                        // the owner's
    SectorBox box;
    int clipMode;                       // the box's clip test verdict (kCrossing, kCulled)
    SectorBox quarters[4];
    int quarterMode[4];
    float minZ;
    float maxZ;
    mfc::CArray<std::uint8_t> waterMaterials;
    // The level of detail (0 every vertex, 1 every second column, 2 every second row too) and the seams
    // towards the sectors before / after it.
    int detail, seamBefore, seamAfter;
    mfc::CArray<SectorPosition> vertices;
    mfc::CArray<SectorVertex, RecordElements> records;
    std::vector<int> tileTextures;      // per tile, ground then water tiles
    std::uint8_t tileRotation[0x100];
    std::vector<int> waterTiles;        // water material per tile, -1 none
    mfc::CArray<FlagArray, FlagArrayElements> tileFlags[4];  // per quarter, per page
    mfc::CArray<TileList, TileListElements> tiles[4];        // per quarter, per page (9 per tile)
    std::vector<AnimatedTile> animated;
    std::vector<int> classCounts;       // 16 texture classes
    int lastUsed;                       // owner frame of the last use
    int relightCountdown;               // frames until the lighting is dropped (RelightTick), -1 none
    std::uint8_t spare38C;              // cleared with the rest; nothing reads it
    std::uint8_t pad_38D[3];
    int spare390;                       // cleared with the rest; nothing reads it

    // Vertex positions and water tiles. Returns 1, or FatalError's result for a missing or malformed file.
    int Load(void* archive, int sx, int sy, int pages, int perPage, const char* prefix, Terrain* owner);

    // Builds the vertex records, tile lists, bounding boxes and water materials.
    void Build();

    // Raises the water layer by the owner's pending water level changes.
    void ApplyWaterLevels();

    // `wanted`: 2 geometry, 4 lighting. Returns wanted & built.
    std::uint32_t Ensure(std::uint32_t wanted, Terrain* owner);

    // Also sizes the shared projection buffers (positions, projected, outcodes) for 0x330 vertices.
    SectorData* Construct();
    void DestructBody();

    // The quarters not culled, at `detail`, ground then water: each quarter's mode becomes its clip result
    // (2 for -1); a crossing quarter's records get their outcodes, the others 0. Returns clipMode.
    int Project(CTransformPipeline* pipeline);

    // The vertices x0..x1, y0..y1 (inclusive, 0..32) of the water layer (where it has a material), then of
    // the ground, with the FPU in chop rounding. Without skipSun relightCountdown becomes -1.
    void Light(CLight* sun, Terrain* owner, int skipSun, int x0, int y0, int x1, int y1);
    // Ambient and `sun`, both dimmed under water on the ground layer; where the sun's ray is blocked
    // (shadows on, ground layer) ambient + material colour, else max(sun shading, ambient).
    void ShadeSun(CLight* sun, const SectorPosition* position, SectorVertex* record, Terrain* owner, int layer);
    // Maxima per channel; flag 0x40 lights count as specular.
    void LightVertex(const SectorPosition* position, SectorVertex* record, Terrain* owner, int layer);
    // LightVertex's specular word alone, for the water layer when the GPU lights the colours.
    void LightSpecular(const SectorPosition* position, SectorVertex* record, Terrain* owner, int layer);

    // The per-quarter arrays keep their sizes.
    void Clear();
    // Sector (-1, -1), pages / perPage -1, clipMode 2, the rest 0.
    void ResetFields();
    // Starts or shortens the relight countdown to `frames`.
    void ScheduleRelight(int frames);
    // At 0 the lighting is dropped, so the next draw relights the sector.
    void RelightTick();
    // Every third frame, with owner flag 2, the animated tiles step to their next frame (one whose frame is 0
    // and whose speed is below 1000 only when an ISAAC draw % 1000 + 1 is within the speed); with owner flag 1
    // and water, the water vertices move with the waves and the crests get a specular colour.
    void Animate(Terrain* owner, int frame);

    void SetDetail(int level);

    // Whether the last clip test did not cull the sector.
    int IsDrawn() const;
    // The box, and unless culled its quarters, with the renderer's pipeline.
    void ClipTest(CSceneRenderer* renderer);
    // At detail 0 whether the lit sectors before / after in y are drawn at detail 1, at detail 1 whether
    // those before / after in x are drawn at detail 2.
    void FindSeams();
};
static_assert(offsetof(SectorData, minZ) == 0x154);
static_assert(offsetof(SectorData, vertices) == 0x17C);

struct TerrainMaterial {
    int kind;                           // 4: water that keeps the ground's x, y
    Vector3 waveColour;                 // specular of the wave crests (Animate)
    float opacity;                      // as the map file has it
    float absorption;                   // light lost per squared depth under water (lighting): 1 / (1 - opacity)
    std::uint8_t opacityByte;           // opacity * 255
    std::uint8_t pad_19[3];
    Vector3 colour;                     // waveColour * the file's light factor, each capped at 1
    std::uint8_t unused_28[4];          // not written by the map loader
    float priority;                     // lowest wins where water materials meet; also scales the waves
    float warpSpeed;                    // from the map file; nothing reads it
    float level;                        // water level offset
};
static_assert(sizeof(TerrainMaterial) == 0x38);

static_assert(offsetof(TerrainMaterial, priority) == 0x2C);

} // namespace terrain
