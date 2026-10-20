#pragma once

// Terrain sectors (game.exe 006BF5F0-006C5410): the 32x32-cell blocks of the landscape, loaded on
// demand from the map's resource archive (<zone>XXXYYY.sec) and turned into per-vertex records and
// per-texture tile lists. Names are provisional. The landscape object itself is `Terrain` (terrain.h).
//
// A sector has 33x33 vertices per layer (ground, and water when the sector has water) and 16x16
// tiles of 2x2 cells. The tiles are grouped into four 8x8-tile quarters; each quarter keeps, per
// texture page, a byte array of tile edge flags and a list of tile entries (3x3 vertices each).

#include <cstddef>
#include <cstdint>

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

// One entry per lit vertex: the point lights that reach it (pooled 16-byte nodes, 006CAC10).
struct LightRef {
    LightRef* next;
    LightRef* prev;
    LightRefBlock* block;   // the pool block it was taken from
    CLight* light;
};

// The LightRef pool (terrain_lights.cpp): blocks of 1000 nodes from operator new. Blocks with free
// nodes are listed from 007C8840, full ones from 007C8844 (doubly linked, newest first); a block
// whose nodes are all free again is deleted.
struct LightRefBlock {
    static constexpr int kRefs = 1000;
    int freeCount;          // +0x00
    LightRefBlock* next;    // +0x04
    LightRefBlock* prev;    // +0x08
    LightRef* freeList;     // +0x0C
    LightRef refs[kRefs];   // +0x10
};
static_assert(sizeof(LightRefBlock) == 0x3E90);

// A node from the pool (inlined in 006CAC10); its next / prev / light are the caller's to set.
LightRef* AllocLightRef();
// 006C5230 (RET 4). Returns `ref` to its block.
void __stdcall FreeLightRef(LightRef* ref);

#pragma pack(push, 1)
// Per-vertex record, 0x54 bytes (odd offsets: the original packs it).
struct SectorVertex {
    Vector4 projected;                  // +0x00 screen position (Project 006BFC80)
    std::uint8_t outcode;               // +0x10 clip flags when the quarter crosses the view, else 0
    std::uint32_t colour;               // +0x11 A (materialByte) R G B, the lighting stage (006C31C0)
    std::uint32_t specular;             // +0x15 edge fade << 24, then the specular lights' R G B
    Vector3 position;                   // +0x19 water layer: the vertex position
    std::uint16_t waterMaterial;        // +0x25 water layer: material index, 0xFFFF none
    float depth;                        // +0x27 ground layer: water z - ground z
    Vector3 normal;                     // +0x2B
    Vector3 lit;                        // +0x37 ambient and sun light (006C36B0)
    Vector3 materialColour;             // +0x43 water material +0x1C..+0x24
    std::uint8_t materialByte;          // +0x4F water material +0x18, 0xFF none
    LightRef* lights;                   // +0x50

    // 006C5150 (terrain_lights.cpp). Returns every node of `lights` to the pool.
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
    int speed;              // from the table at 00796960 by texture class
};
static_assert(sizeof(AnimatedTile) == 0x18);

// A bounding box: flags (bit 1 valid), centre, then the low and high corner offsets.
struct SectorBox {
    std::uint32_t flags;
    Vector3 centre;
    float radius;           // +0x10 ClipBounds::radius (BoundsOf); nothing here writes it
    Vector3 low;
    Vector3 high;
    std::uint32_t unused_2C[3];
};
static_assert(sizeof(SectorBox) == 0x38);
// A box is a ClipBounds (model.h) followed by three unused dwords (asserted in terrain_sector.cpp).
inline const ClipBounds* BoundsOf(const SectorBox& box) { return reinterpret_cast<const ClipBounds*>(&box); }

// CTransformPipeline::ClipTest's verdicts as the sector keeps them (+0x60, quarterMode): 0 inside
// the view, 1 crossing its edge, 2 outside (-1 is stored as 2).
constexpr int kCrossing = 1;
constexpr int kCulled = 2;

// A vertex position of a sector layer (x, y, z).
struct SectorPosition : Vector3 {};
static_assert(sizeof(SectorPosition) == 12);

// The construct / destroy helpers of the sector's CArray instantiations (mfc::CArray's Elements
// policy): the records (006C4F10 zeroes, 006C4EE0 releases their lights), the per-page tile flag
// CByteArrays (006C4F90 / 006C4F60) and the per-page tile lists (006C5050 / 006C5010).
// 006C4EE0 (RET 8; terrain_lights.cpp). ReleaseLights for `count` records.
void __stdcall DestroyRecords(SectorVertex* records, int count);

// 006CAC10 (RET 0x14; terrain_lights.cpp). For the vertices of `sector` inside `area` (the whole
// sector where it lies inside the area's sector range): `light` is removed from a vertex's list
// when `remove` is set or the vertex is not nearer than its range to `position`; otherwise it is
// added at the list's head when missing.
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

// A 32x32-cell block of terrain (0x394 bytes, constructed by 006BF5F0). Each sector holds two
// layers of 33x33 vertices (x, y, z): the ground, and the water surface when the sector has water.
struct SectorData {
    int DrawPage(void* context, void* /*unused*/, int page, int /*unused*/);
    std::uint8_t hasSpecular;           // +0x000 a specular light reached a vertex (Light clears it)
    std::uint8_t pad_001[3];
    std::uint32_t built;                // +0x004 1 loaded, 2 geometry, 4 lit
    int sx, sy;                         // +0x008
    int hasWater;                       // +0x010
    int hasWaterTiles;                  // +0x014 a second tile layer (water tiles)
    const std::uint8_t* data;           // +0x018 the .sec file after its 5-byte header
    Terrain* owner;               // +0x01C
    int pages;                          // +0x020 owner +0x11C
    int perPage;                        // +0x024 owner +0x128
    SectorBox box;                      // +0x028
    int clipMode;                       // +0x060 the box's clip test verdict (kCrossing, kCulled)
    SectorBox quarters[4];              // +0x064
    int quarterMode[4];                 // +0x144
    float minZ;                         // +0x154
    float maxZ;                         // +0x158
    mfc::CArray<std::uint8_t> waterMaterials;  // +0x15C
    // +0x170 the level of detail (SetDetail: 0 every vertex, 1 every second column, 2 every second
    // row too); +0x174 / +0x178 the seams towards the sectors before / after it (FindSeams).
    int detail, seamBefore, seamAfter;
    mfc::CArray<SectorPosition> vertices;      // +0x17C
    mfc::CArray<SectorVertex, RecordElements> records;   // +0x190
    lib::AlignedArray<int> tileTextures;      // +0x1A4 per tile, ground then water tiles
    std::uint8_t tileRotation[0x100];   // +0x1B4
    lib::AlignedArray<int> waterTiles;        // +0x2B4 water material per tile, -1 none
    mfc::CArray<FlagArray, FlagArrayElements> tileFlags[4];  // +0x2C4 per quarter, per page
    mfc::CArray<TileList, TileListElements> tiles[4];        // +0x314 per quarter, per page (9 per tile)
    lib::AlignedArray<AnimatedTile> animated;  // +0x364
    lib::AlignedArray<int> classCounts;       // +0x374 16 texture classes
    int lastUsed;                       // +0x384 owner frame of the last use
    int relightCountdown;               // +0x388 frames until the lighting is dropped (RelightTick), -1 none
    std::uint8_t spare38C;              // +0x38C cleared with the rest; nothing reads it
    std::uint8_t pad_38D[3];
    int spare390;                       // +0x390 cleared with the rest; nothing reads it

    // 006C13A0. Loads sector (sx, sy) from `archive` (the owner's CMappedResource): vertex
    // positions and water tiles. Returns 1, or FatalError's result for a missing or malformed file.
    int Load(void* archive, int sx, int sy, int pages, int perPage, const char* prefix, Terrain* owner);

    // 006C1C00. Builds the vertex records, tile lists, bounding boxes and water materials.
    void Build();

    // 006C30E0. Raises the water layer by the owner's pending water level changes.
    void ApplyWaterLevels();

    // 006C3DD0. Builds what `wanted` asks for (2 geometry, 4 lighting); returns wanted & built.
    std::uint32_t Ensure(std::uint32_t wanted, Terrain* owner);

    // 006BF5F0. The constructor body: everything empty or zero, sector (-1, -1), pages and
    // perPage -1, +0x60 2; the shared projection buffers (positions, projected, outcodes) sized for
    // 0x330 vertices. Returns this.
    SectorData* Construct();
    // 006BF8A0 (terrain_sector.cpp, A-H21). The destructor body: Clear, then the members in reverse
    // order (each array's elements destroyed, its storage freed, its vtable written first).
    void DestructBody();

    // 006BFC80 (RET 4; the draw pass 006C6460). Projects the vertices of every quarter that is not
    // culled (quarterMode != 2), at the level of detail `detail` (from 1 every second column,
    // from 2 every second row too), ground then water, with `pipeline` (004F29F0) against the
    // quarter's box: the quarter's
    // mode becomes the result (2 for -1 or 2), and for 0 / 1 each of those records gets its
    // projected position (with 1 its outcode, else 0). Returns clipMode.
    int Project(CTransformPipeline* pipeline);

    // 006C3A00 (RET 0x1C; terrain_lighting.cpp). Lights the vertices x0..x1, y0..y1 (inclusive,
    // 0..32) of the water layer (where it has a material), then of the ground, with the FPU in chop
    // rounding: the sun and ambient light first (ShadeSun) unless `skipSun`, then the point lights
    // (LightVertex). Sets built bit 4; without skipSun relightCountdown becomes -1.
    void Light(CLight* sun, Terrain* owner, int skipSun, int x0, int y0, int x1, int y1);
    // 006C36B0 (RET 0x14). The record's lit colour: ambient (owner +0x224) and `sun`, both dimmed
    // under water on the ground layer; where the sun's ray is blocked (shadows on, ground layer)
    // ambient + material colour, else max(sun shading, ambient).
    void ShadeSun(CLight* sun, const SectorPosition* position, SectorVertex* record, Terrain* owner, int layer);
    // 006C31C0 (RET 0x10). The record's colour and specular words from its lit colour and the point
    // lights in its list (maxima per channel; flag 0x40 lights count as specular).
    void LightVertex(const SectorPosition* position, SectorVertex* record, Terrain* owner, int layer);

    // terrain_animation.cpp:
    // 006C3C80. Empties the records (their lights released), each per-page tile flag array and
    // tile list (the per-quarter arrays keep their sizes), then ResetFields.
    void Clear();
    // 006C3D70. Sector (-1, -1), nothing built, no data, pages / perPage -1, clipMode 2, the rest 0.
    void ResetFields();
    // 006C3E30 (RET 4, from the light countdown 006C8ED0). Starts or shortens the relight
    // countdown to `frames`.
    void ScheduleRelight(int frames);
    // 006C3E50 (each frame for a lit sector). Counts the relight countdown down; at 0 the lighting
    // is dropped (built bit 4 cleared, countdown -1), so the next draw relights the sector.
    void RelightTick();
    // 006C3E80 (RET 8; each frame for a drawn sector). Every third frame, with owner flag 2, the
    // animated tiles step to their next frame (one whose frame is 0 and whose speed is below 1000
    // only when an ISAAC draw % 1000 + 1 is within the speed) and get its texture coordinates; with
    // owner flag 1 and water, the water vertices move with the waves and the crests get a specular
    // colour.
    void Animate(Terrain* owner, int frame);

    // 006C1390 (RET 4; terrain_passes.cpp). The level of detail (`detail`) the sector is drawn
    // at: 0 every vertex, 1 every second column, 2 every second row too.
    void SetDetail(int level);

    // 006BFB00 (terrain_passes.cpp). 1 when clipMode is 0 or 1 (the last clip test did not cull the
    // sector), else 0.
    int IsDrawn() const;
    // 006BFB20 (RET 4; terrain_passes.cpp). The clip test of the sector's box (clipMode: 0 inside,
    // 1 crossing, 2 culled; -1 counts as culled) and, unless culled, of its quarters (quarterMode)
    // with the renderer's pipeline (004F16A0).
    void ClipTest(CSceneRenderer* renderer);
    // 006BFB80 (terrain_passes.cpp). The seams (seamBefore / seamAfter): at detail 0 whether
    // the lit sectors before / after in y are drawn at detail 1, at detail 1 whether those before /
    // after in x are drawn at detail 2.
    void FindSeams();
};
static_assert(sizeof(SectorData) == 0x394);
static_assert(offsetof(SectorData, minZ) == 0x154);
static_assert(offsetof(SectorData, vertices) == 0x17C);
static_assert(offsetof(SectorData, tileFlags) == 0x2C4);
static_assert(offsetof(SectorData, tiles) == 0x314);
static_assert(offsetof(SectorData, animated) == 0x364);
static_assert(offsetof(SectorData, lastUsed) == 0x384);

// A terrain material, 0x38 bytes (owner +0x12C).
struct TerrainMaterial {
    int kind;                           // +0x00 4: water that keeps the ground's x, y
    Vector3 waveColour;                 // +0x04 specular of the wave crests (Animate)
    float opacity;                      // +0x10 as the map file has it
    float absorption;                   // +0x14 light lost per squared depth under water (lighting): 1 / (1 - opacity)
    std::uint8_t opacityByte;           // +0x18 opacity * 255
    std::uint8_t pad_19[3];
    Vector3 colour;                     // +0x1C waveColour * the file's light factor, each capped at 1
    std::uint8_t unused_28[4];          // not written by the map loader
    float priority;                     // +0x2C lowest wins where water materials meet; also scales the waves
    float warpSpeed;                    // +0x30 the file record's +0x1C (MapMaterial); nothing reads it
    float level;                        // +0x34 water level offset
};
static_assert(sizeof(TerrainMaterial) == 0x38);

static_assert(offsetof(TerrainMaterial, priority) == 0x2C);

} // namespace terrain
