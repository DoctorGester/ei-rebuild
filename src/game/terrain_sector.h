#pragma once

// Terrain sectors (game.exe 006BF5F0-006C5410): the 32x32-cell blocks of the landscape, loaded on
// demand from the map's resource archive (<zone>XXXYYY.sec) and turned into per-vertex records and
// per-texture tile lists. Names are provisional. The landscape object itself (`Terrain`, terrain.h)
// is viewed here through `TerrainFields`, which names only the fields this code uses.
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
class CTransformPipeline;

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
    float projected[4];                 // +0x00 screen position (Project 006BFC80)
    std::uint8_t outcode;               // +0x10 clip flags when the quarter crosses the view, else 0
    std::uint32_t colour;               // +0x11 A (materialByte) R G B, the lighting stage (006C31C0)
    std::uint32_t specular;             // +0x15 edge fade << 24, then the specular lights' R G B
    float position[3];                  // +0x19 water layer: the vertex position
    std::uint16_t waterMaterial;        // +0x25 water layer: material index, 0xFFFF none
    float depth;                        // +0x27 ground layer: water z - ground z
    float normal[3];                    // +0x2B
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
    float centre[3];
    std::uint32_t unknown_10;
    float low[3];
    float high[3];
    std::uint32_t unknown_2C[3];
};
static_assert(sizeof(SectorBox) == 0x38);

struct TerrainFields;

// A vertex position of a sector layer (x, y, z).
struct SectorPosition {
    float xyz[3];
};
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

// 0x394 bytes, constructed by 006BF5F0.
struct SectorData {
    std::uint8_t hasSpecular;           // +0x000 a specular light reached a vertex (Light clears it)
    std::uint8_t pad_001[3];
    std::uint32_t built;                // +0x004 1 loaded, 2 geometry, 4 lit
    int sx, sy;                         // +0x008
    int hasWater;                       // +0x010
    int hasWaterTiles;                  // +0x014 a second tile layer (water tiles)
    const std::uint8_t* data;           // +0x018 the .sec file after its 5-byte header
    TerrainFields* owner;               // +0x01C
    int pages;                          // +0x020 owner +0x11C
    int perPage;                        // +0x024 owner +0x128
    SectorBox box;                      // +0x028
    std::uint32_t unknown_060;
    SectorBox quarters[4];              // +0x064
    int quarterMode[4];                 // +0x144
    float minZ;                         // +0x154
    float maxZ;                         // +0x158
    mfc::CArray<std::uint8_t> waterMaterials;  // +0x15C
    int unknown_170, unknown_174, unknown_178;
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
    std::uint8_t unknown_38C;
    std::uint8_t pad_38D[3];
    int unknown_390;

    // 006C13A0. Loads sector (sx, sy) from `archive` (the owner's CMappedResource): vertex
    // positions and water tiles. Returns 1, or FatalError's result for a missing or malformed file.
    int Load(void* archive, int sx, int sy, int pages, int perPage, const char* prefix, TerrainFields* owner);

    // 006C1C00. Builds the vertex records, tile lists, bounding boxes and water materials.
    void Build();

    // 006C30E0. Raises the water layer by the owner's pending water level changes.
    void ApplyWaterLevels();

    // 006C3DD0. Builds what `wanted` asks for (2 geometry, 4 lighting); returns wanted & built.
    std::uint32_t Ensure(std::uint32_t wanted, TerrainFields* owner);

    // 006BF5F0. The constructor body: everything empty or zero, sector (-1, -1), pages and
    // perPage -1, +0x60 2; the shared projection buffers (positions, projected, outcodes) sized for
    // 0x330 vertices. Returns this.
    SectorData* Construct();
    // 006BF8A0 (terrain_sector.cpp, A-H21). The destructor body: Clear, then the members in reverse
    // order (each array's elements destroyed, its storage freed, its vtable written first).
    void DestructBody();

    // 006BFC80 (RET 4; the draw pass 006C6460). Projects the vertices of every quarter that is not
    // culled (quarterMode != 2), at the level of detail unknown_170 (from 1 every second column,
    // from 2 every second row too), ground then water, with `pipeline` (004F29F0) against the
    // quarter's box: the quarter's
    // mode becomes the result (2 for -1 or 2), and for 0 / 1 each of those records gets its
    // projected position (with 1 its outcode, else 0). Returns +0x60.
    int Project(CTransformPipeline* pipeline);

    // 006C3A00 (RET 0x1C; terrain_lighting.cpp). Lights the vertices x0..x1, y0..y1 (inclusive,
    // 0..32) of the water layer (where it has a material), then of the ground, with the FPU in chop
    // rounding: the sun and ambient light first (ShadeSun) unless `skipSun`, then the point lights
    // (LightVertex). Sets built bit 4; without skipSun relightCountdown becomes -1.
    void Light(CLight* sun, TerrainFields* owner, int skipSun, int x0, int y0, int x1, int y1);
    // 006C36B0 (RET 0x14). The record's lit colour: ambient (owner +0x224) and `sun`, both dimmed
    // under water on the ground layer; where the sun's ray is blocked (shadows on, ground layer)
    // ambient + material colour, else max(sun shading, ambient).
    void ShadeSun(CLight* sun, const SectorPosition* position, SectorVertex* record, TerrainFields* owner, int layer);
    // 006C31C0 (RET 0x10). The record's colour and specular words from its lit colour and the point
    // lights in its list (maxima per channel; flag 0x40 lights count as specular).
    void LightVertex(const SectorPosition* position, SectorVertex* record, TerrainFields* owner, int layer);

    // terrain_animation.cpp:
    // 006C3C80. Empties the records (their lights released), each per-page tile flag array and
    // tile list (the per-quarter arrays keep their sizes), then ResetFields.
    void Clear();
    // 006C3D70. Sector (-1, -1), nothing built, no data, pages / perPage -1, +0x60 2, the rest 0.
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
    void Animate(TerrainFields* owner, int frame);

    // Not rewritten (forwarders).
    void Destruct();                    // 006BF8A0 the destructor body (terrain_landscape.cpp)
    int IsDrawn();                      // 006BFB00 +0x60 is 0 or 1 (the last Project did not cull it)
    void BuildAtOriginal();             // 006C1C00 through its address (lands on the installed hook)
    void LightAtOriginal(CLight* sun, TerrainFields* owner, int skipSun, int x0, int y0, int x1, int y1); // 006C3A00
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
    std::uint8_t unknown_10[4];
    float absorption;                   // +0x14 light lost per squared depth under water (lighting)
    std::uint8_t byte18;                // +0x18
    std::uint8_t pad_19[3];
    Vector3 colour;                     // +0x1C
    std::uint8_t unknown_28[4];
    float priority;                     // +0x2C lowest wins where water materials meet; also scales the waves
    std::uint8_t unknown_30[4];
    float level;                        // +0x34 water level offset
};
static_assert(sizeof(TerrainMaterial) == 0x38);

// The landscape object (vtable 0073F6A8), only the fields used here.
struct TerrainFields {
    std::uint8_t unknown_000[0x94];
    std::uint32_t flags;                // +0x094 1 water waves, 2 animated tiles, 4 fade the light at the
                                        // map edges, 8 ambient sounds, 0x10 sun shadows
    std::uint8_t unknown_098[0x104 - 0x98];
    int sectorsX;                       // +0x104
    int sectorsY;                       // +0x108
    std::uint8_t unknown_10C[0x114 - 0x10C];
    float heightScale;                  // +0x114
    std::uint8_t unknown_118[0x11C - 0x118];
    int pages;                          // +0x11C
    std::uint8_t unknown_120[0x128 - 0x120];
    int perPage;                        // +0x128
    TerrainMaterial* materials;         // +0x12C
    std::uint8_t unknown_130[0x13C - 0x130];
    int* textureClass;                  // +0x13C per texture, 0..15
    std::uint8_t unknown_140[0x14C - 0x140];
    int* textureFrames;                 // +0x14C per texture, frames when animated
    std::uint8_t unknown_150[0x1C8 - 0x150];
    float* uvTables[4];                 // +0x1C8 per rotation, 0x48 bytes per texture of a page
    float* uvTablesLast[4];             // +0x1D8 the same for the last two pages
    VertexTiles* vertexTiles;           // +0x1E8
    std::uint8_t unknown_1EC[0x224 - 0x1EC];
    Vector4 ambient;                    // +0x224 the ambient light colour (r, g, b, a)
    std::uint8_t unknown_234[0x24C - 0x234];   // the sun's ray steps (006C92C0)
    SectorData* lightingSector;         // +0x24C the vertex being lit, for the shadow test
    int lightingX;                      // +0x250
    int lightingY;                      // +0x254
    CLight* sun;                        // +0x258
    std::uint8_t unknown_25C[0x274 - 0x25C];
    Vector3 waveDirection;              // +0x274 unit vector (006C7D70); x, y: the waves' travel
    float waveHeight;                   // +0x280
    float waveGlow;                     // +0x284 scales the crests' specular colour
    float wavePhase;                    // +0x288 advanced by waveSpeed every frame
    float waveSpeed;                    // +0x28C
    lib::AlignedArray<float> wavePhases;   // +0x290 per water vertex (33 x 33): phase offsets (006C57E0)
    float sine[512];                    // +0x2A0 sin(i * pi / 256)
    std::uint8_t unknown_AA0[0xAEC - 0xAA0];
    mfc::CArray<float>* waterLevels;    // +0xAEC per sector, per material

    // 006C53C0. The 18 floats (9 u, then 9 v) of texture `index` of `page` under `rotation`.
    const float* TileUv(int rotation, int index, int page);
    // 006C8D40. Texture class (sound / prints material) at (x, y): the water tile's when the
    // point is under water, 8 outside the map.
    int SurfaceClass(float x, float y);

    // 006C92C0 (RET 4, forwarder). 0 when the sun's ray from `position` (the vertex lightingX /
    // lightingY of lightingSector) is blocked by the terrain, else nonzero; loads the sectors it
    // crosses.
    int SunReaches(const SectorPosition* position);
};
static_assert(offsetof(TerrainFields, materials) == 0x12C);
static_assert(offsetof(TerrainFields, vertexTiles) == 0x1E8);
static_assert(offsetof(TerrainFields, waterLevels) == 0xAEC);
static_assert(offsetof(TerrainFields, flags) == 0x94 && offsetof(TerrainFields, ambient) == 0x224 &&
              offsetof(TerrainFields, sun) == 0x258);
static_assert(offsetof(TerrainFields, waveDirection) == 0x274 && offsetof(TerrainFields, wavePhases) == 0x290 &&
              offsetof(TerrainFields, sine) == 0x2A0 && offsetof(TerrainMaterial, priority) == 0x2C);

} // namespace terrain
