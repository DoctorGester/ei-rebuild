#pragma once

// Terrain sectors: the 32x32-cell blocks of the landscape, loaded on demand from the map's resource archive
// (<zone>XXXYYY.sec) and turned into per-vertex records and per-texture tile lists. Names are provisional.
// The landscape object itself is `Terrain` (terrain.h). A sector has 33x33 vertices per layer (ground, and
// water when the sector has water) and 16x16 tiles of 2x2 cells. The tiles are grouped into four 8x8-tile
// quarters; each quarter keeps, per texture page, a byte array of tile edge flags and a list of tile
// entries (3x3 vertices each).

#include <cstddef>
#include <cstdint>

#include "model.h"     // ClipBounds
#include "terrain.h"

#include "lib/vecmath.h"

#include "lib/containers.h"
#include "lib/mfc.h"

class CLight;
class CSceneRenderer;
class CTransformPipeline;
struct ClipBounds;

namespace terrain {

struct LightRefBlock;

// One entry per lit vertex: the point lights that reach it (pooled 16-byte nodes).
struct LightRef {
    LightRef* next;
    LightRef* prev;
    LightRefBlock* block;   // the pool block it was taken from
    CLight* light;
};

// The LightRef pool (terrain_lights.cpp): blocks of 1000 nodes from operator new. Blocks with free nodes
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
// Per-vertex record, 0x54 bytes (odd offsets: packed).
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
    Vector3 materialColour;             // water material +0x1C..+0x24
    std::uint8_t materialByte;          // water material +0x18, 0xFF none
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

// A bounding box: a ClipBounds (model.h; flags bit 1 valid, the centre, the corner offsets) and three
// unused dwords.
struct SectorBox : ClipBounds {
    std::uint32_t unused_2C[3];
};
static_assert(sizeof(SectorBox) == 0x38);
inline const ClipBounds* BoundsOf(const SectorBox& box) { return &box; }

// CTransformPipeline::ClipTest's verdicts as the sector keeps them (+0x60, quarterMode): 0 inside
// the view, 1 crossing its edge, 2 outside (-1 is stored as 2).
constexpr int kCrossing = 1;
constexpr int kCulled = 2;

// A vertex position of a sector layer (x, y, z).
struct SectorPosition : Vector3 {};
static_assert(sizeof(SectorPosition) == 12);

// The construct / destroy helpers of the sector's CArray instantiations (mfc::CArray's Elements policy):
// the records (zeroed; their lights released), the per-page tile flag CByteArrays and the per-page tile
// lists.
// ReleaseLights for `count` records.
void __stdcall DestroyRecords(SectorVertex* records, int count);

// For the vertices of `sector` inside `area` (the whole sector where it lies inside the area's sector
// range): `light` is removed from a vertex's list when `remove` is set or the vertex is not nearer than its
// range to `position`; otherwise it is added at the list's head when missing.
void __stdcall UpdateVertexLights(SectorData* sector, CLight* light, const Vector3* position, const LightArea* area,
                                  int remove);

struct RecordElements {
    static void Construct(SectorVertex* records, int count);
    static void Destruct(SectorVertex* records, int count);
};
using FlagArray = mfc::CArray<std::uint8_t>;      // CByteArray
struct FlagArrayElements {
    static void Construct(FlagArray* arrays, int count);
    static void Destruct(FlagArray* arrays, int count);
};
using TileList = lib::AlignedArray<TileVertex>;
struct TileListElements {
    static void Construct(TileList* lists, int count);
    static void Destruct(TileList* lists, int count);
};

// A 32x32-cell block of terrain. Each sector holds two layers of 33x33 vertices (x, y, z):
// the ground, and the water surface when the sector has water.
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
    int pages;                          // owner +0x11C
    int perPage;                        // owner +0x128
    SectorBox box;
    int clipMode;                       // the box's clip test verdict (kCrossing, kCulled)
    SectorBox quarters[4];
    int quarterMode[4];
    float minZ;
    float maxZ;
    mfc::CArray<std::uint8_t> waterMaterials;
    // +0x170 the level of detail (SetDetail: 0 every vertex, 1 every second column, 2 every second
    // row too); +0x174 / +0x178 the seams towards the sectors before / after it (FindSeams).
    int detail, seamBefore, seamAfter;
    mfc::CArray<SectorPosition> vertices;
    mfc::CArray<SectorVertex, RecordElements> records;
    lib::AlignedArray<int> tileTextures;      // per tile, ground then water tiles
    std::uint8_t tileRotation[0x100];
    lib::AlignedArray<int> waterTiles;        // water material per tile, -1 none
    mfc::CArray<FlagArray, FlagArrayElements> tileFlags[4];  // per quarter, per page
    mfc::CArray<TileList, TileListElements> tiles[4];        // per quarter, per page (9 per tile)
    lib::AlignedArray<AnimatedTile> animated;
    lib::AlignedArray<int> classCounts;       // 16 texture classes
    int lastUsed;                       // owner frame of the last use
    int relightCountdown;               // frames until the lighting is dropped (RelightTick), -1 none
    std::uint8_t spare38C;              // cleared with the rest; nothing reads it
    std::uint8_t pad_38D[3];
    int spare390;                       // cleared with the rest; nothing reads it

    // Loads sector (sx, sy) from `archive` (the owner's CMappedResource): vertex
    // positions and water tiles. Returns 1, or FatalError's result for a missing or malformed file.
    int Load(void* archive, int sx, int sy, int pages, int perPage, const char* prefix, Terrain* owner);

    // Builds the vertex records, tile lists, bounding boxes and water materials.
    void Build();

    // Raises the water layer by the owner's pending water level changes.
    void ApplyWaterLevels();

    // Builds what `wanted` asks for (2 geometry, 4 lighting); returns wanted & built.
    std::uint32_t Ensure(std::uint32_t wanted, Terrain* owner);

    // The constructor body: everything empty or zero, sector (-1, -1), pages and
    // perPage -1, +0x60 2; the shared projection buffers (positions, projected, outcodes) sized for
    // 0x330 vertices.
    SectorData* Construct();
    // The destructor body: Clear, then the members in reverse order (each array's elements destroyed, its
    // storage freed, its vtable written first).
    void DestructBody();

    // Projects the vertices of every quarter that is not culled (quarterMode != 2), at the level of detail
    // `detail` (from 1 every second column, from 2 every second row too), ground then water, with
    // `pipeline` against the quarter's box: the quarter's mode becomes the result (2 for -1 or 2), and for
    // 0 / 1 each of those records gets its projected position (with 1 its outcode, else 0). Returns
    // clipMode.
    int Project(CTransformPipeline* pipeline);

    // Lights the vertices x0..x1, y0..y1 (inclusive, 0..32) of the water layer (where it has a material),
    // then of the ground, with the FPU in chop rounding: the sun and ambient light first (ShadeSun) unless
    // `skipSun`, then the point lights (LightVertex). Sets built bit 4; without skipSun relightCountdown
    // becomes -1.
    void Light(CLight* sun, Terrain* owner, int skipSun, int x0, int y0, int x1, int y1);
    // The record's lit colour: ambient and `sun`, both dimmed
    // under water on the ground layer; where the sun's ray is blocked (shadows on, ground layer)
    // ambient + material colour, else max(sun shading, ambient).
    void ShadeSun(CLight* sun, const SectorPosition* position, SectorVertex* record, Terrain* owner, int layer);
    // The record's colour and specular words from its lit colour and the point
    // lights in its list (maxima per channel; flag 0x40 lights count as specular).
    void LightVertex(const SectorPosition* position, SectorVertex* record, Terrain* owner, int layer);
    // LightVertex's specular word alone (terrain_lighting.cpp), for the water layer when the GPU lights the
    // colours.
    void LightSpecular(const SectorPosition* position, SectorVertex* record, Terrain* owner, int layer);

    // terrain_animation.cpp:
    // Empties the records (their lights released), each per-page tile flag array and
    // tile list (the per-quarter arrays keep their sizes), then ResetFields.
    void Clear();
    // Sector (-1, -1), nothing built, no data, pages / perPage -1, clipMode 2, the rest 0.
    void ResetFields();
    // Starts or shortens the relight countdown to `frames`.
    void ScheduleRelight(int frames);
    // Each frame for a lit sector: counts the relight countdown down; at 0 the lighting is dropped (built
    // bit 4 cleared, countdown -1), so the next draw relights the sector.
    void RelightTick();
    // Each frame for a drawn sector: every third frame, with owner flag 2, the animated tiles step to their
    // next frame (one whose frame is 0 and whose speed is below 1000 only when an ISAAC draw % 1000 + 1 is
    // within the speed) and get its texture coordinates; with owner flag 1 and water, the water vertices
    // move with the waves and the crests get a specular colour.
    void Animate(Terrain* owner, int frame);

    // The level of detail (`detail`) the sector is drawn at: 0 every vertex, 1 every second column, 2 every
    // second row too.
    void SetDetail(int level);

    // 1 when clipMode is 0 or 1 (the last clip test did not cull the sector), else 0.
    int IsDrawn() const;
    // The clip test of the sector's box (clipMode: 0 inside, 1 crossing, 2 culled; -1 counts as culled)
    // and, unless culled, of its quarters (quarterMode) with the renderer's pipeline.
    void ClipTest(CSceneRenderer* renderer);
    // The seams (seamBefore / seamAfter): at detail 0 whether the lit sectors before / after in y are drawn
    // at detail 1, at detail 1 whether those before / after in x are drawn at detail 2.
    void FindSeams();
};
static_assert(sizeof(SectorData) == 0x394);
static_assert(offsetof(SectorData, minZ) == 0x154);
static_assert(offsetof(SectorData, vertices) == 0x17C);
static_assert(offsetof(SectorData, tileFlags) == 0x2C4);
static_assert(offsetof(SectorData, tiles) == 0x314);
static_assert(offsetof(SectorData, animated) == 0x364);
static_assert(offsetof(SectorData, lastUsed) == 0x384);

// A terrain material, 0x38 bytes.
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
    float warpSpeed;                    // the file record's +0x1C (MapMaterial); nothing reads it
    float level;                        // water level offset
};
static_assert(sizeof(TerrainMaterial) == 0x38);

static_assert(offsetof(TerrainMaterial, priority) == 0x2C);

} // namespace terrain
